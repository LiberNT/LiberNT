/*
 * PROJECT:     LiberNT Windows Error Reporting host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Out-of-process crash and hang reporting, runtime exception modules and hang recovery
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winuser.h>
#include <winver.h>
#include <dbghelp.h>
#define PSAPI_VERSION 1
#include <psapi.h>
#include <rpc.h>
#include <strsafe.h>
#include <errorrep.h>
#include <werapi.h>
#define NTOS_MODE_USER
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>
#include <basewer.h>
#include <werremote.h>
#include "resource.h"

#define WER_SETTINGS_KEY    L"Software\\Microsoft\\Windows\\Windows Error Reporting"
#define WER_EXCLUDED_KEY    WER_SETTINGS_KEY L"\\ExcludedApplications"
#define WER_HELPERS_KEY     WER_SETTINGS_KEY L"\\RuntimeExceptionHelperModules"
#define WER_LOCAL_DUMPS_KEY WER_SETTINGS_KEY L"\\LocalDumps"
#define AEDEBUG_KEY         L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug"
#define WER_HANG_EVENT      L"AppHangB1"

typedef struct _WER_TARGET
{
    DWORD ProcessId;
    DWORD ThreadId;
    HANDLE Process;
    HANDLE Thread;
    ULONG64 RegistrationAddress;
    BASE_WER_REGISTRATION Registration;
    DWORD Flags;
    BOOL Suspended;
    WCHAR ImagePath[MAX_PATH];
} WER_TARGET, *PWER_TARGET;

typedef struct _WER_CLAIM
{
    BOOL Claimed;
    DWORD SignatureCount;
    WCHAR EventName[MAX_PATH];
    WCHAR Names[WER_MAX_PARAM_COUNT][WER_MAX_SIGNATURE_NAME_LENGTH + 1];
    WCHAR Values[WER_MAX_PARAM_COUNT][WER_MAX_PARAM_LENGTH];
    BOOL CustomDebugger;
    BOOL AutoLaunch;
    WCHAR Debugger[MAX_PATH * 2];
} WER_CLAIM, *PWER_CLAIM;

typedef struct _WER_LOCAL_DUMPS
{
    WCHAR Folder[MAX_PATH];
    DWORD Count;
    DWORD Type;
    DWORD CustomFlags;
} WER_LOCAL_DUMPS, *PWER_LOCAL_DUMPS;

typedef struct _WER_DUMP_FILE
{
    FILETIME Created;
    WCHAR Name[MAX_PATH];
} WER_DUMP_FILE, *PWER_DUMP_FILE;

typedef struct _WER_RECOVERY_DIALOG
{
    PWER_TARGET Target;
    PCWSTR AppName;
} WER_RECOVERY_DIALOG, *PWER_RECOVERY_DIALOG;

static
DWORD
WerpReadSetting(
    _In_ PCWSTR Name,
    _In_ DWORD Default)
{
    static const HKEY Roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    DWORD Value, Size, Type;
    ULONG Index;
    LONG Error;
    HKEY Key;

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
WerpHasValue(
    _In_ HKEY Root,
    _In_ PCWSTR KeyName,
    _In_ PCWSTR ValueName)
{
    LONG Error;
    HKEY Key;

    if (!ValueName[0] || RegOpenKeyExW(Root, KeyName, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;

    Error = RegQueryValueExW(Key, ValueName, NULL, NULL, NULL, NULL);
    RegCloseKey(Key);
    return Error == ERROR_SUCCESS;
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
BOOL
WerpParseNumber(
    _In_ PCWSTR Text,
    _Out_ PULONG64 Value)
{
    PWSTR End;

    *Value = _wcstoui64(Text, &End, 0);
    return End != Text && *End == UNICODE_NULL;
}

static
BOOL
WerpParseArguments(
    _Out_ PWCHAR Mode,
    _Out_ PULONG64 ProcessId,
    _Out_ PULONG64 ThreadId,
    _Out_ PULONG64 Extra)
{
    WCHAR CommandLine[512];
    PWSTR Cursor, Token, Value;
    BOOL Quoted = FALSE;
    WCHAR Option;

    *Mode = UNICODE_NULL;
    *ProcessId = *ThreadId = *Extra = 0;
    if (FAILED(StringCchCopyW(CommandLine, ARRAYSIZE(CommandLine), GetCommandLineW())))
        return FALSE;

    for (Cursor = CommandLine; *Cursor && (Quoted || *Cursor != L' '); Cursor++)
    {
        if (*Cursor == L'"')
            Quoted = !Quoted;
    }

    for (Token = wcstok(Cursor, L" "); Token; Token = wcstok(NULL, L" "))
    {
        if (Token[0] != L'-' || !Token[1] || Token[2])
            return FALSE;

        Option = Token[1];
        if (Option == L'u' || Option == L'h')
        {
            *Mode = Option;
            continue;
        }

        Value = wcstok(NULL, L" ");
        if (!Value)
            return FALSE;

        switch (Option)
        {
            case L'p':
                if (!WerpParseNumber(Value, ProcessId))
                    return FALSE;
                break;
            case L't':
                if (!WerpParseNumber(Value, ThreadId))
                    return FALSE;
                break;
            case L'x':
                if (!WerpParseNumber(Value, Extra))
                    return FALSE;
                break;
            default:
                return FALSE;
        }
    }

    return *Mode && *ProcessId && *ThreadId;
}

static
BOOL
WerpOpenTarget(
    _In_ DWORD ProcessId,
    _In_ DWORD ThreadId,
    _Out_ PWER_TARGET Target)
{
    DWORD Size = ARRAYSIZE(Target->ImagePath);

    RtlZeroMemory(Target, sizeof(*Target));
    Target->ProcessId = ProcessId;
    Target->ThreadId = ThreadId;
    Target->Process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, ProcessId);
    if (!Target->Process)
        return FALSE;

    Target->Thread = OpenThread(THREAD_ALL_ACCESS, FALSE, ThreadId);
    if (!QueryFullProcessImageNameW(Target->Process, 0, Target->ImagePath, &Size))
        Target->ImagePath[0] = UNICODE_NULL;

    if (WerRemoteReadRegistration(Target->Process, &Target->RegistrationAddress, &Target->Registration) == S_OK &&
        (Target->Registration.State & BASE_WER_FLAGS_SET))
    {
        Target->Flags = Target->Registration.Flags;
    }

    return TRUE;
}

static
VOID
WerpSuspendTarget(
    _Inout_ PWER_TARGET Target)
{
    if (!(Target->Flags & WER_FAULT_REPORTING_FLAG_DISABLE_THREAD_SUSPENSION))
        Target->Suspended = NT_SUCCESS(NtSuspendProcess(Target->Process));
}

static
VOID
WerpResumeTarget(
    _Inout_ PWER_TARGET Target)
{
    if (Target->Suspended)
        NtResumeProcess(Target->Process);
    Target->Suspended = FALSE;
}

static
VOID
WerpCloseTarget(
    _Inout_ PWER_TARGET Target)
{
    WerpResumeTarget(Target);
    if (Target->Thread)
        CloseHandle(Target->Thread);
    if (Target->Process)
        CloseHandle(Target->Process);
}

static
BOOL
WerpIsReportingEnabled(
    _In_ PWER_TARGET Target)
{
    PCWSTR Image = WerpFileName(Target->ImagePath);

    return !WerpReadSetting(L"Disabled", 0) &&
           !WerpHasValue(HKEY_LOCAL_MACHINE, WER_EXCLUDED_KEY, Image) &&
           !WerpHasValue(HKEY_CURRENT_USER, WER_EXCLUDED_KEY, Image);
}

static
VOID
WerpApplyLocalDumpValues(
    _In_ HKEY Key,
    _Inout_ PWER_LOCAL_DUMPS Settings)
{
    static const PCWSTR Names[] = { L"DumpCount", L"DumpType", L"CustomDumpFlags" };
    PDWORD Values[] = { &Settings->Count, &Settings->Type, &Settings->CustomFlags };
    WCHAR Folder[MAX_PATH];
    DWORD Type, Size, Value;
    ULONG Index;

    Size = sizeof(Folder) - sizeof(WCHAR);
    if (RegQueryValueExW(Key, L"DumpFolder", NULL, &Type, (PBYTE)Folder, &Size) == ERROR_SUCCESS &&
        (Type == REG_EXPAND_SZ || Type == REG_SZ))
    {
        Folder[Size / sizeof(WCHAR)] = UNICODE_NULL;
        StringCchCopyW(Settings->Folder, ARRAYSIZE(Settings->Folder), Folder);
    }

    for (Index = 0; Index < ARRAYSIZE(Names); Index++)
    {
        Size = sizeof(Value);
        if (RegQueryValueExW(Key, Names[Index], NULL, &Type, (PBYTE)&Value, &Size) == ERROR_SUCCESS &&
            Type == REG_DWORD && Size == sizeof(Value))
        {
            *Values[Index] = Value;
        }
    }
}

static
BOOL
WerpReadLocalDumpSettings(
    _In_ PCWSTR Image,
    _Out_ PWER_LOCAL_DUMPS Settings)
{
    WCHAR Folder[MAX_PATH];
    HKEY Key, ApplicationKey;
    DWORD Length;

    StringCchCopyW(Settings->Folder, ARRAYSIZE(Settings->Folder), L"%LOCALAPPDATA%\\CrashDumps");
    Settings->Count = 10;
    Settings->Type = 1;
    Settings->CustomFlags = MiniDumpWithDataSegs | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, WER_LOCAL_DUMPS_KEY, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;

    WerpApplyLocalDumpValues(Key, Settings);
    if (Image[0] && RegOpenKeyExW(Key, Image, 0, KEY_QUERY_VALUE, &ApplicationKey) == ERROR_SUCCESS)
    {
        WerpApplyLocalDumpValues(ApplicationKey, Settings);
        RegCloseKey(ApplicationKey);
    }
    RegCloseKey(Key);

    Length = ExpandEnvironmentStringsW(Settings->Folder, Folder, ARRAYSIZE(Folder));
    if (!Length || Length > ARRAYSIZE(Folder))
        return FALSE;

    StringCchCopyW(Settings->Folder, ARRAYSIZE(Settings->Folder), Folder);
    return TRUE;
}

static
BOOL
WerpIsAutoDebugging(VOID)
{
    WCHAR Auto[8], Debugger[MAX_PATH];
    DWORD Type, Size;
    BOOL Enabled = FALSE;
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, AEDEBUG_KEY, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;

    Size = sizeof(Auto) - sizeof(WCHAR);
    if (RegQueryValueExW(Key, L"Auto", NULL, &Type, (PBYTE)Auto, &Size) == ERROR_SUCCESS && Type == REG_SZ)
    {
        Auto[Size / sizeof(WCHAR)] = UNICODE_NULL;
        Size = sizeof(Debugger) - sizeof(WCHAR);
        Enabled = Auto[0] == L'1' &&
                  RegQueryValueExW(Key, L"Debugger", NULL, &Type, (PBYTE)Debugger, &Size) == ERROR_SUCCESS &&
                  Size >= 2 * sizeof(WCHAR);
    }

    RegCloseKey(Key);
    return Enabled;
}

static
int
__cdecl
WerpCompareDumpFiles(
    _In_ const void *Left,
    _In_ const void *Right)
{
    return CompareFileTime(&((const WER_DUMP_FILE *)Left)->Created, &((const WER_DUMP_FILE *)Right)->Created);
}

static
VOID
WerpPruneLocalDumps(
    _In_ PCWSTR Folder,
    _In_ DWORD Keep)
{
    WCHAR Pattern[MAX_PATH], Path[MAX_PATH];
    PWER_DUMP_FILE Files = NULL, Grown;
    ULONG Count = 0, Capacity = 0, Index;
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    if (FAILED(StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*.dmp", Folder)))
        return;

    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        if (Count == Capacity)
        {
            Capacity = Capacity ? Capacity * 2 : 16;
            Grown = Files ? HeapReAlloc(GetProcessHeap(), 0, Files, Capacity * sizeof(*Files)) :
                            HeapAlloc(GetProcessHeap(), 0, Capacity * sizeof(*Files));
            if (!Grown)
                break;
            Files = Grown;
        }

        Files[Count].Created = Data.ftCreationTime;
        StringCchCopyW(Files[Count].Name, ARRAYSIZE(Files[Count].Name), Data.cFileName);
        Count++;
    } while (FindNextFileW(Find, &Data));
    FindClose(Find);

    if (Count)
        qsort(Files, Count, sizeof(*Files), WerpCompareDumpFiles);

    for (Index = 0; Count > Keep && Index < Count - Keep; Index++)
    {
        if (SUCCEEDED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Folder, Files[Index].Name)))
            DeleteFileW(Path);
    }

    if (Files)
        HeapFree(GetProcessHeap(), 0, Files);
}

static
VOID
WerpWriteLocalDump(
    _In_ PWER_TARGET Target,
    _In_ ULONG64 PointersAddress,
    _In_ BOOL HaveException)
{
    MINIDUMP_EXCEPTION_INFORMATION Exception;
    WCHAR Partial[MAX_PATH], Path[MAX_PATH];
    WER_LOCAL_DUMPS Settings;
    MINIDUMP_TYPE Type;
    PWSTR Cursor;
    HANDLE File;
    BOOL Success;

    if (!WerpReadLocalDumpSettings(WerpFileName(Target->ImagePath), &Settings) || WerpIsAutoDebugging())
        return;

    /* FIXME: the flag sets of the mini and full LocalDumps types are undocumented: the WER minidump set and full memory are used */
    if (Settings.Type == 0)
        Type = Settings.CustomFlags;
    else if (Settings.Type == 2)
        Type = MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithUnloadedModules |
               MiniDumpWithProcessThreadData | MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo;
    else
        Type = MiniDumpWithDataSegs | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData |
               MiniDumpWithTokenInformation;

    StringCchCopyW(Partial, ARRAYSIZE(Partial), Settings.Folder);
    for (Cursor = wcschr(Partial, L'\\'); Cursor; Cursor = wcschr(Cursor + 1, L'\\'))
    {
        if (Cursor - Partial <= 2)
            continue;
        *Cursor = UNICODE_NULL;
        CreateDirectoryW(Partial, NULL);
        *Cursor = L'\\';
    }
    CreateDirectoryW(Partial, NULL);

    WerpPruneLocalDumps(Settings.Folder, max(Settings.Count, 1) - 1);
    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s.%lu.dmp", Settings.Folder,
                                WerpFileName(Target->ImagePath), Target->ProcessId)))
    {
        return;
    }

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return;

    Exception.ThreadId = Target->ThreadId;
    Exception.ExceptionPointers = (PEXCEPTION_POINTERS)(ULONG_PTR)PointersAddress;
    Exception.ClientPointers = TRUE;
    Success = MiniDumpWriteDump(Target->Process, Target->ProcessId, File, Type, HaveException ? &Exception : NULL,
                                NULL, NULL);
    CloseHandle(File);
    if (!Success)
        DeleteFileW(Path);
}

static
VOID
WerpFileVersion(
    _In_ PCWSTR Path,
    _Out_writes_(Size) PWSTR Text,
    _In_ DWORD Size)
{
    VS_FIXEDFILEINFO *Info;
    DWORD Handle, Length;
    UINT InfoSize;
    PVOID Data;

    StringCchCopyW(Text, Size, L"0.0.0.0");
    if (!Path[0] || !(Length = GetFileVersionInfoSizeW(Path, &Handle)))
        return;

    Data = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Data)
        return;

    if (GetFileVersionInfoW(Path, 0, Length, Data) && VerQueryValueW(Data, L"\\", (PVOID *)&Info, &InfoSize) &&
        InfoSize >= sizeof(*Info))
    {
        StringCchPrintfW(Text, Size, L"%u.%u.%u.%u", HIWORD(Info->dwFileVersionMS), LOWORD(Info->dwFileVersionMS),
                         HIWORD(Info->dwFileVersionLS), LOWORD(Info->dwFileVersionLS));
    }

    HeapFree(GetProcessHeap(), 0, Data);
}

static
DWORD
WerpImageTimestamp(
    _In_ HANDLE Process,
    _In_ ULONG_PTR Base)
{
    IMAGE_DOS_HEADER Dos;
    DWORD Signature;
    IMAGE_FILE_HEADER Header;

    if (!Base ||
        !ReadProcessMemory(Process, (PVOID)Base, &Dos, sizeof(Dos), NULL) || Dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !ReadProcessMemory(Process, (PVOID)(Base + Dos.e_lfanew), &Signature, sizeof(Signature), NULL) ||
        Signature != IMAGE_NT_SIGNATURE ||
        !ReadProcessMemory(Process, (PVOID)(Base + Dos.e_lfanew + sizeof(Signature)), &Header, sizeof(Header), NULL))
    {
        return 0;
    }

    return Header.TimeDateStamp;
}

static
ULONG_PTR
WerpProcessImageBase(
    _In_ HANDLE Process)
{
    PROCESS_BASIC_INFORMATION BasicInformation;
    ULONG_PTR Base = 0;

    if (!NT_SUCCESS(NtQueryInformationProcess(Process, ProcessBasicInformation, &BasicInformation,
                                              sizeof(BasicInformation), NULL)) ||
        !BasicInformation.PebBaseAddress ||
        !ReadProcessMemory(Process, (PUCHAR)BasicInformation.PebBaseAddress + FIELD_OFFSET(PEB, ImageBaseAddress),
                           &Base, sizeof(Base), NULL))
    {
        return 0;
    }

    return Base;
}

static
VOID
WerpSetApplicationParameters(
    _In_ HREPORT Report,
    _In_ PWER_TARGET Target)
{
    WCHAR Version[32], Stamp[16];

    WerpFileVersion(Target->ImagePath, Version, ARRAYSIZE(Version));
    StringCchPrintfW(Stamp, ARRAYSIZE(Stamp), L"%08lx", WerpImageTimestamp(Target->Process,
                                                                            WerpProcessImageBase(Target->Process)));
    WerReportSetParameter(Report, WER_P0, L"Application Name", WerpFileName(Target->ImagePath));
    WerReportSetParameter(Report, WER_P1, L"Application Version", Version);
    WerReportSetParameter(Report, WER_P2, L"Application Timestamp", Stamp);
}

static
VOID
WerpSetCrashParameters(
    _In_ HREPORT Report,
    _In_ PWER_TARGET Target,
    _In_opt_ PEXCEPTION_RECORD Record)
{
    WCHAR ModulePath[MAX_PATH], Version[32], Stamp[16], Code[16], Offset[32];
    MEMORY_BASIC_INFORMATION Memory;
    ULONG_PTR Address = Record ? (ULONG_PTR)Record->ExceptionAddress : 0, Base = 0;

    WerpSetApplicationParameters(Report, Target);

    StringCchCopyW(ModulePath, ARRAYSIZE(ModulePath), L"unknown");
    if (Address && VirtualQueryEx(Target->Process, (PVOID)Address, &Memory, sizeof(Memory)) &&
        Memory.Type == MEM_IMAGE &&
        GetModuleFileNameExW(Target->Process, (HMODULE)Memory.AllocationBase, ModulePath, ARRAYSIZE(ModulePath)))
    {
        Base = (ULONG_PTR)Memory.AllocationBase;
    }

    WerpFileVersion(Base ? ModulePath : L"", Version, ARRAYSIZE(Version));
    StringCchPrintfW(Stamp, ARRAYSIZE(Stamp), L"%08lx", WerpImageTimestamp(Target->Process, Base));
    StringCchPrintfW(Code, ARRAYSIZE(Code), L"%08lx", Record ? (ULONG)Record->ExceptionCode : 0);
    StringCchPrintfW(Offset, ARRAYSIZE(Offset), L"%0*Ix", (int)(sizeof(ULONG_PTR) * 2), Address - Base);
    WerReportSetParameter(Report, WER_P3, L"Fault Module Name", WerpFileName(ModulePath));
    WerReportSetParameter(Report, WER_P4, L"Fault Module Version", Version);
    WerReportSetParameter(Report, WER_P5, L"Fault Module Timestamp", Stamp);
    WerReportSetParameter(Report, WER_P6, L"Exception Code", Code);
    WerReportSetParameter(Report, WER_P7, L"Exception Offset", Offset);
}

static
BOOL
WerpReadException(
    _In_ PWER_TARGET Target,
    _In_ ULONG64 PointersAddress,
    _Out_ PEXCEPTION_RECORD Record,
    _Out_ PCONTEXT Context)
{
    EXCEPTION_POINTERS Pointers;
    BOOL TargetWow64 = FALSE, SelfWow64 = FALSE;

    RtlZeroMemory(Record, sizeof(*Record));
    RtlZeroMemory(Context, sizeof(*Context));
    IsWow64Process(Target->Process, &TargetWow64);
    IsWow64Process(GetCurrentProcess(), &SelfWow64);
    if (!PointersAddress || PointersAddress != (ULONG_PTR)PointersAddress || TargetWow64 != SelfWow64)
        return FALSE;

    return ReadProcessMemory(Target->Process, (PVOID)(ULONG_PTR)PointersAddress, &Pointers, sizeof(Pointers), NULL) &&
           ReadProcessMemory(Target->Process, Pointers.ExceptionRecord, Record, sizeof(*Record), NULL) &&
           ReadProcessMemory(Target->Process, Pointers.ContextRecord, Context, sizeof(*Context), NULL);
}

static
VOID
WerpRunExceptionModules(
    _In_ PWER_TARGET Target,
    _In_ PWER_RUNTIME_EXCEPTION_INFORMATION Information,
    _Out_ PWER_CLAIM Claim,
    _Out_ HMODULE *ClaimingModule)
{
    PFN_WER_RUNTIME_EXCEPTION_DEBUGGER_LAUNCH DebuggerCallback;
    PFN_WER_RUNTIME_EXCEPTION_EVENT_SIGNATURE SignatureCallback;
    PFN_WER_RUNTIME_EXCEPTION_EVENT EventCallback;
    PWER_REMOTE_ENTRY Modules;
    DWORD Size, NameSize, ValueSize, Index;
    ULONG Count, Module;
    BOOL Claimed, Failed;
    HMODULE Library;
    PVOID Context;
    HRESULT Result;

    RtlZeroMemory(Claim, sizeof(*Claim));
    *ClaimingModule = NULL;
    if (WerRemoteReadEntries(Target->Process, Target->Registration.RuntimeModules,
                             WER_MAX_REGISTERED_RUNTIME_EXCEPTION_MODULES, &Modules, &Count) != S_OK)
    {
        return;
    }

    for (Module = 0; Module < Count && !Claim->Claimed; Module++)
    {
        if (!WerpHasValue(HKEY_LOCAL_MACHINE, WER_HELPERS_KEY, Modules[Module].Name))
            continue;

        Library = LoadLibraryW(Modules[Module].Name);
        if (!Library)
            continue;

        EventCallback = (PFN_WER_RUNTIME_EXCEPTION_EVENT)GetProcAddress(Library, WER_RUNTIME_EXCEPTION_EVENT_FUNCTION);
        SignatureCallback = (PFN_WER_RUNTIME_EXCEPTION_EVENT_SIGNATURE)
                            GetProcAddress(Library, WER_RUNTIME_EXCEPTION_EVENT_SIGNATURE_FUNCTION);
        DebuggerCallback = (PFN_WER_RUNTIME_EXCEPTION_DEBUGGER_LAUNCH)
                           GetProcAddress(Library, WER_RUNTIME_EXCEPTION_DEBUGGER_LAUNCH);
        Context = (PVOID)(ULONG_PTR)Modules[Module].Address;
        Claimed = FALSE;
        Size = ARRAYSIZE(Claim->EventName);
        Claim->SignatureCount = 0;
        Result = EventCallback ? EventCallback(Context, Information, &Claimed, Claim->EventName, &Size,
                                               &Claim->SignatureCount) : E_NOTIMPL;
        if (FAILED(Result) || !Claimed)
        {
            FreeLibrary(Library);
            continue;
        }

        Failed = !SignatureCallback || !Claim->EventName[0];
        Claim->EventName[ARRAYSIZE(Claim->EventName) - 1] = UNICODE_NULL;
        Claim->SignatureCount = min(Claim->SignatureCount, WER_MAX_PARAM_COUNT);
        for (Index = 0; !Failed && Index < Claim->SignatureCount; Index++)
        {
            NameSize = ARRAYSIZE(Claim->Names[Index]);
            ValueSize = ARRAYSIZE(Claim->Values[Index]);
            Failed = FAILED(SignatureCallback(Context, Information, Index, Claim->Names[Index], &NameSize,
                                              Claim->Values[Index], &ValueSize));
            Claim->Names[Index][ARRAYSIZE(Claim->Names[Index]) - 1] = UNICODE_NULL;
            Claim->Values[Index][ARRAYSIZE(Claim->Values[Index]) - 1] = UNICODE_NULL;
        }

        if (!Failed && DebuggerCallback)
        {
            Size = ARRAYSIZE(Claim->Debugger);
            Failed = FAILED(DebuggerCallback(Context, Information, &Claim->CustomDebugger, Claim->Debugger, &Size,
                                             &Claim->AutoLaunch));
            Claim->Debugger[ARRAYSIZE(Claim->Debugger) - 1] = UNICODE_NULL;
        }

        if (Failed)
        {
            FreeLibrary(Library);
            RtlZeroMemory(Claim, sizeof(*Claim));
            break;
        }

        Claim->Claimed = TRUE;
        *ClaimingModule = Library;
    }

    HeapFree(GetProcessHeap(), 0, Modules);
}

static
BOOL
WerpFormatDebugger(
    _In_ PCWSTR Format,
    _In_ DWORD ProcessId,
    _In_ HANDLE Event,
    _Out_writes_(Size) PWSTR CommandLine,
    _In_ DWORD Size)
{
    PCWSTR Cursor;
    ULONG Specifiers = 0;

    for (Cursor = wcschr(Format, L'%'); Cursor; Cursor = wcschr(Cursor + 2, L'%'))
    {
        if (Cursor[1] == L'%')
            continue;
        if (Cursor[1] != L'l' || Cursor[2] != L'd' || ++Specifiers > 2)
            return FALSE;
    }

    return SUCCEEDED(StringCchPrintfW(CommandLine, Size, Format, (LONG)ProcessId, (LONG)(ULONG_PTR)Event));
}

static
BOOL
WerpLaunchDebugger(
    _In_ PWER_TARGET Target,
    _In_ PCWSTR Format)
{
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    WCHAR CommandLine[MAX_PATH * 2 + 32];
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo;
    HANDLE Handles[2];
    BOOL Attached = FALSE;

    Handles[0] = CreateEventW(&Attributes, TRUE, FALSE, NULL);
    if (!Handles[0])
        return FALSE;

    RtlZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    if (WerpFormatDebugger(Format, Target->ProcessId, Handles[0], CommandLine, ARRAYSIZE(CommandLine)) &&
        CreateProcessW(NULL, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        WerpResumeTarget(Target);
        Handles[1] = ProcessInfo.hProcess;
        Attached = WaitForMultipleObjects(ARRAYSIZE(Handles), Handles, FALSE, INFINITE) == WAIT_OBJECT_0;
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);
    }

    CloseHandle(Handles[0]);
    return Attached;
}

static
BOOL
WerpSubmit(
    _In_ HREPORT Report,
    _In_ PWER_TARGET Target,
    _In_opt_ PWER_EXCEPTION_INFORMATION Exception)
{
    WER_SUBMIT_RESULT Result = WerReportFailed;

    WerReportAddDump(Report, Target->Process, Target->Thread, WerDumpTypeMiniDump, Exception, NULL, 0);
    if (Target->Flags & (WER_FAULT_REPORTING_FLAG_QUEUE | WER_FAULT_REPORTING_FLAG_QUEUE_UPLOAD))
        WerReportAddDump(Report, Target->Process, Target->Thread, WerDumpTypeHeapDump, Exception, NULL, 0);

    return SUCCEEDED(WerReportSubmit(Report, WerConsentNotAsked, WER_SUBMIT_QUEUE | WER_SUBMIT_ADD_REGISTERED_DATA,
                                     &Result)) && Result == WerReportQueued;
}

static
DWORD
WerpReportCrash(
    _In_ DWORD ProcessId,
    _In_ DWORD ThreadId,
    _In_ ULONG64 PointersAddress)
{
    WER_REPORT_INFORMATION ReportInformation;
    WER_RUNTIME_EXCEPTION_INFORMATION Information;
    WER_EXCEPTION_INFORMATION Exception;
    WCHAR ReportId[40];
    RPC_WSTR UuidText;
    HMODULE ClaimingModule = NULL;
    PWER_CLAIM Claim;
    WER_TARGET Target;
    HREPORT Report;
    DWORD Verdict = frrvErr, Index;
    BOOL HaveException, Queued = FALSE;
    UUID Uuid;

    if (!WerpOpenTarget(ProcessId, ThreadId, &Target))
        return frrvErrNoDW;

    WerpSuspendTarget(&Target);
    RtlZeroMemory(&Information, sizeof(Information));
    Information.dwSize = sizeof(Information);
    Information.hProcess = Target.Process;
    Information.hThread = Target.Thread;
    HaveException = WerpReadException(&Target, PointersAddress, &Information.exceptionRecord, &Information.context);
    WerpWriteLocalDump(&Target, PointersAddress, HaveException);

    if (!WerpIsReportingEnabled(&Target))
    {
        WerpCloseTarget(&Target);
        return frrvErrNoDW;
    }

    Claim = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Claim));
    if (!Claim)
    {
        WerpCloseTarget(&Target);
        return frrvErr;
    }

    ReportId[0] = UNICODE_NULL;
    if (UuidCreate(&Uuid) == RPC_S_OK && UuidToStringW(&Uuid, &UuidText) == RPC_S_OK)
    {
        StringCchCopyW(ReportId, ARRAYSIZE(ReportId), (PCWSTR)UuidText);
        RpcStringFreeW(&UuidText);
    }
    Information.pwszReportId = ReportId;

    if (HaveException)
        WerpRunExceptionModules(&Target, &Information, Claim, &ClaimingModule);

    RtlZeroMemory(&ReportInformation, sizeof(ReportInformation));
    ReportInformation.dwSize = sizeof(ReportInformation);
    ReportInformation.hProcess = Target.Process;
    StringCchCopyW(ReportInformation.wzApplicationPath, ARRAYSIZE(ReportInformation.wzApplicationPath),
                   Target.ImagePath);

    Exception.pExceptionPointers = (PEXCEPTION_POINTERS)(ULONG_PTR)PointersAddress;
    Exception.bClientPointers = TRUE;
    if (SUCCEEDED(WerReportCreate(Claim->Claimed ? Claim->EventName : APPCRASH_EVENT, WerReportApplicationCrash,
                                  &ReportInformation, &Report)))
    {
        if (Claim->Claimed)
        {
            for (Index = 0; Index < Claim->SignatureCount; Index++)
                WerReportSetParameter(Report, Index, Claim->Names[Index][0] ? Claim->Names[Index] : NULL,
                                      Claim->Values[Index]);
        }
        else
        {
            WerpSetCrashParameters(Report, &Target, HaveException ? &Information.exceptionRecord : NULL);
        }

        Queued = WerpSubmit(Report, &Target, HaveException ? &Exception : NULL);
        WerReportCloseHandle(Report);
    }

    Verdict = Queued ? frrvOkQueued : frrvErr;
    /* FIXME: no prompt UI, so a custom debugger that is not auto-launched is never offered */
    if (Claim->Claimed && Claim->CustomDebugger && Claim->AutoLaunch && Claim->Debugger[0] &&
        WerpLaunchDebugger(&Target, Claim->Debugger))
    {
        Verdict = frrvLaunchDebugger;
    }

    if (ClaimingModule)
        FreeLibrary(ClaimingModule);
    HeapFree(GetProcessHeap(), 0, Claim);
    WerpCloseTarget(&Target);
    return Verdict;
}

static
INT_PTR
CALLBACK
WerpRecoveryDialogProc(
    _In_ HWND Dialog,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    PWER_RECOVERY_DIALOG Context = (PWER_RECOVERY_DIALOG)GetWindowLongPtrW(Dialog, DWLP_USER);
    WCHAR Format[128], Text[MAX_PATH + 128];
    LONG Cancelled = TRUE;

    switch (Message)
    {
        case WM_INITDIALOG:
            Context = (PWER_RECOVERY_DIALOG)LParam;
            SetWindowLongPtrW(Dialog, DWLP_USER, LParam);
            if (LoadStringW(GetModuleHandleW(NULL), IDS_RECOVERING, Format, ARRAYSIZE(Format)) &&
                SUCCEEDED(StringCchPrintfW(Text, ARRAYSIZE(Text), Format, Context->AppName)))
            {
                SetDlgItemTextW(Dialog, IDC_RECOVERY_TEXT, Text);
            }
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(WParam) != IDCANCEL || !Context)
                break;

            WriteProcessMemory(Context->Target->Process,
                               (PVOID)(ULONG_PTR)(Context->Target->RegistrationAddress +
                                                  FIELD_OFFSET(BASE_WER_REGISTRATION, RecoveryCancelled)),
                               &Cancelled, sizeof(Cancelled), NULL);
            EnableWindow(GetDlgItem(Dialog, IDCANCEL), FALSE);
            if (LoadStringW(GetModuleHandleW(NULL), IDS_CANCELLING, Format, ARRAYSIZE(Format)) &&
                SUCCEEDED(StringCchPrintfW(Text, ARRAYSIZE(Text), Format, Context->AppName)))
            {
                SetDlgItemTextW(Dialog, IDC_RECOVERY_TEXT, Text);
            }
            return TRUE;
    }

    return FALSE;
}

static
VOID
WerpRunHangRecovery(
    _In_ PWER_TARGET Target)
{
    APPLICATION_RECOVERY_CALLBACK Callback;
    WER_RECOVERY_DIALOG DialogContext;
    DWORD PingInterval = 0, Wait, Now, Deadline;
    HANDLE Events[2], RemoteEvent = NULL, RemoteThread;
    LONG Cancelled = FALSE;
    HWND Dialog = NULL;
    MSG Message;

    if (GetApplicationRecoveryCallback(Target->Process, &Callback, NULL, &PingInterval, NULL) != S_OK ||
        WerRemoteReadRegistration(Target->Process, &Target->RegistrationAddress, &Target->Registration) != S_OK ||
        !Target->Registration.RemoteRecoveryRoutine)
    {
        return;
    }

    PingInterval = max(PingInterval / 10000, 1);
    Events[0] = Target->Process;
    Events[1] = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!Events[1])
        return;

    if (!DuplicateHandle(GetCurrentProcess(), Events[1], Target->Process, &RemoteEvent, EVENT_MODIFY_STATE, FALSE, 0))
    {
        CloseHandle(Events[1]);
        return;
    }

    WriteProcessMemory(Target->Process,
                       (PVOID)(ULONG_PTR)(Target->RegistrationAddress +
                                          FIELD_OFFSET(BASE_WER_REGISTRATION, RecoveryCancelled)),
                       &Cancelled, sizeof(Cancelled), NULL);

    DialogContext.Target = Target;
    DialogContext.AppName = WerpFileName(Target->ImagePath);
    if (!(Target->Flags & WER_FAULT_REPORTING_NO_UI) && !WerpReadSetting(L"DontShowUI", 0))
    {
        Dialog = CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_RECOVERY), NULL,
                                    WerpRecoveryDialogProc, (LPARAM)&DialogContext);
    }

    RemoteThread = CreateRemoteThread(Target->Process, NULL, 0,
                                      (LPTHREAD_START_ROUTINE)(ULONG_PTR)Target->Registration.RemoteRecoveryRoutine,
                                      RemoteEvent, 0, NULL);
    if (RemoteThread)
    {
        CloseHandle(RemoteThread);
        Deadline = GetTickCount() + PingInterval;
        for (;;)
        {
            Now = GetTickCount();
            if ((LONG)(Deadline - Now) <= 0)
                break;

            Wait = MsgWaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, Deadline - Now,
                                             Dialog ? QS_ALLINPUT : 0);
            if (Wait == WAIT_OBJECT_0 || Wait == WAIT_FAILED)
                break;

            if (Wait == WAIT_OBJECT_0 + 1)
                Deadline = GetTickCount() + PingInterval;

            while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            {
                if (!Dialog || !IsDialogMessageW(Dialog, &Message))
                {
                    TranslateMessage(&Message);
                    DispatchMessageW(&Message);
                }
            }
        }
    }

    if (Dialog)
        DestroyWindow(Dialog);
    CloseHandle(Events[1]);
}

static
DWORD
WerpReportHang(
    _In_ DWORD ProcessId,
    _In_ DWORD ThreadId)
{
    WER_REPORT_INFORMATION ReportInformation;
    WER_TARGET Target;
    HREPORT Report;

    if (!WerpOpenTarget(ProcessId, ThreadId, &Target))
        return ERROR_INVALID_PARAMETER;

    if (WerpIsReportingEnabled(&Target))
    {
        WerpSuspendTarget(&Target);
        RtlZeroMemory(&ReportInformation, sizeof(ReportInformation));
        ReportInformation.dwSize = sizeof(ReportInformation);
        ReportInformation.hProcess = Target.Process;
        StringCchCopyW(ReportInformation.wzApplicationPath, ARRAYSIZE(ReportInformation.wzApplicationPath),
                       Target.ImagePath);
        if (SUCCEEDED(WerReportCreate(WER_HANG_EVENT, WerReportApplicationHang, &ReportInformation, &Report)))
        {
            WerpSetApplicationParameters(Report, &Target);
            WerpSubmit(Report, &Target, NULL);
            WerReportCloseHandle(Report);
        }
        WerpResumeTarget(&Target);
    }

    /* TODO: restart a hung application registered with RegisterApplicationRestart */
    WerpRunHangRecovery(&Target);
    TerminateProcess(Target.Process, BASE_WER_HANG_EXIT_STATUS);
    WaitForSingleObject(Target.Process, 5000);
    WerpCloseTarget(&Target);
    return ERROR_SUCCESS;
}

int
WINAPI
wWinMain(
    _In_ HINSTANCE Instance,
    _In_opt_ HINSTANCE PreviousInstance,
    _In_ PWSTR CommandLine,
    _In_ int ShowCommand)
{
    ULONG64 ProcessId, ThreadId, Extra;
    WCHAR Mode;

    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(PreviousInstance);
    UNREFERENCED_PARAMETER(CommandLine);
    UNREFERENCED_PARAMETER(ShowCommand);

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!WerpParseArguments(&Mode, &ProcessId, &ThreadId, &Extra) || ProcessId > MAXDWORD || ThreadId > MAXDWORD)
        return ERROR_INVALID_PARAMETER;

    if (Mode == L'u')
        return WerpReportCrash((DWORD)ProcessId, (DWORD)ThreadId, Extra);

    return WerpReportHang((DWORD)ProcessId, (DWORD)ThreadId);
}
