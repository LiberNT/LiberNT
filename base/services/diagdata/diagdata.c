/*
 * PROJECT:     LiberNT Diagnostic Data Service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sends the required and the optional diagnostic data, spooled until delivered
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windows.h>
#include <winhttp.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strsafe.h>
#define NTOS_MODE_USER
#include <ndk/mmtypes.h>
#include <reactos/dump.h>
#define NDEBUG
#include <debug.h>

#define DIAGDATA_KEY L"SOFTWARE\\LiberNT\\DiagnosticData"
#define DIAGDATA_ENDPOINT L"https://ci.libernt.com/api/diagnostic-data/v1/report"
#define DIAGDATA_AGENT L"LiberNT-DiagnosticData/1"
#define DIAGDATA_SETTLE_MS 30000
#define DIAGDATA_PNP_POLL_MS 500
#define DIAGDATA_OFF_LEVEL 0
#define DIAGDATA_OPTIONAL_LEVEL 3
#define DIAGDATA_DATACOLLECTION_KEY L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\DataCollection"
#define DIAGDATA_SPOOL_SUBDIR L"\\DiagnosticData\\spool"
#define DIAGDATA_SPOOL_MAX_FILES 32
#define DIAGDATA_SPOOL_MAX_BYTES (1024 * 1024)
#define DIAGDATA_SPOOL_DACL L"D:P(A;;FA;;;SY)(A;;FA;;;BA)"
#define DIAGDATA_MAX_EVENTS 64
#define DIAGDATA_SEND_TRIES 4
#define DIAGDATA_SEND_WAIT_MS 2500
#define DIAGDATA_RETRY_CAP_MS 60000
#define DIAGDATA_HTTP_TIMEOUT_MS 15000
#define DIAGDATA_MAX_DEVICES 512
#define DIAGDATA_MEMORY_GRANULE_MB 256
#define DIAGDATA_TEXT_LENGTH 256
#define DIAGDATA_URL_LENGTH 1024
#define DIAGDATA_SMBIOS_HEADER 8
#define DIAGDATA_SMBIOS_SYSTEM 1
#define DIAGDATA_SMBIOS_END 127
#define DIAGDATA_MAX_APP_CRASHES 32
#define DIAGDATA_EVENT_BUFFER 65536
#define DIAGDATA_APPLICATION_ERROR 1000
#define DIAGDATA_APPLICATION_ERROR_STRINGS 8
#define DIAGDATA_UNIX_EPOCH 116444736000000000ULL
#define DIAGDATA_FILETIME_PER_SECOND 10000000ULL

typedef struct _DIAGDATA_BUFFER
{
    PCHAR Data;
    SIZE_T Length;
    SIZE_T Capacity;
    BOOL Failed;
} DIAGDATA_BUFFER, *PDIAGDATA_BUFFER;

typedef struct _DIAGDATA_PROGRESS
{
    ULONGLONG KernelCrash;
    DWORD AppCrash;
    DWORD SystemEvent;
} DIAGDATA_PROGRESS, *PDIAGDATA_PROGRESS;

typedef struct _DIAGDATA_STATE
{
    DIAGDATA_PROGRESS Reported;
    ULONGLONG BootMilliseconds;
    PCSTR PreviousShutdown;
    WCHAR InstallId[40];
    WCHAR Channel[64];
    WCHAR Endpoint[DIAGDATA_URL_LENGTH];
} DIAGDATA_STATE, *PDIAGDATA_STATE;

static const WCHAR ServiceName[] = L"DiagData";
static SERVICE_STATUS_HANDLE ServiceStatusHandle;
static SERVICE_STATUS ServiceStatus;
static HANDLE StopEvent;
static HKEY DiagDataKey;
static DIAGDATA_STATE DiagDataState;

static BOOL BufferReserve(_Inout_ PDIAGDATA_BUFFER Buffer, _In_ SIZE_T Extra)
{
    SIZE_T Capacity;
    PCHAR Data;

    if (Buffer->Failed)
        return FALSE;
    if (Buffer->Length + Extra <= Buffer->Capacity)
        return TRUE;

    Capacity = max(Buffer->Capacity * 2, Buffer->Length + Extra + 4096);
    Data = Buffer->Data ? HeapReAlloc(GetProcessHeap(), 0, Buffer->Data, Capacity) : HeapAlloc(GetProcessHeap(), 0, Capacity);
    if (Data == NULL)
    {
        Buffer->Failed = TRUE;
        return FALSE;
    }
    Buffer->Data = Data;
    Buffer->Capacity = Capacity;
    return TRUE;
}

static VOID BufferText(_Inout_ PDIAGDATA_BUFFER Buffer, _In_ PCSTR Text)
{
    SIZE_T Length = strlen(Text);

    if (!BufferReserve(Buffer, Length))
        return;
    CopyMemory(Buffer->Data + Buffer->Length, Text, Length);
    Buffer->Length += Length;
}

static VOID BufferNumber(_Inout_ PDIAGDATA_BUFFER Buffer, _In_ ULONGLONG Value)
{
    CHAR Text[24];

    _snprintf(Text, ARRAYSIZE(Text), "%I64u", Value);
    Text[ARRAYSIZE(Text) - 1] = ANSI_NULL;
    BufferText(Buffer, Text);
}

static VOID BufferString(_Inout_ PDIAGDATA_BUFFER Buffer, _In_ PCWSTR Value)
{
    static const CHAR Hex[] = "0123456789abcdef";
    PCHAR Utf8;
    INT Length;
    INT Index;
    UCHAR Character;

    BufferText(Buffer, "\"");
    Length = WideCharToMultiByte(CP_UTF8, 0, Value, -1, NULL, 0, NULL, NULL);
    if (Length > 1)
    {
        Utf8 = HeapAlloc(GetProcessHeap(), 0, Length);
        if (Utf8 == NULL)
        {
            Buffer->Failed = TRUE;
            return;
        }
        WideCharToMultiByte(CP_UTF8, 0, Value, -1, Utf8, Length, NULL, NULL);
        for (Index = 0; Index < Length - 1 && BufferReserve(Buffer, 6); Index++)
        {
            Character = (UCHAR)Utf8[Index];
            if (Character == '"' || Character == '\\')
            {
                Buffer->Data[Buffer->Length++] = '\\';
                Buffer->Data[Buffer->Length++] = (CHAR)Character;
            }
            else if (Character < 0x20)
            {
                CopyMemory(Buffer->Data + Buffer->Length, "\\u00", 4);
                Buffer->Data[Buffer->Length + 4] = Hex[Character >> 4];
                Buffer->Data[Buffer->Length + 5] = Hex[Character & 0xF];
                Buffer->Length += 6;
            }
            else
            {
                Buffer->Data[Buffer->Length++] = (CHAR)Character;
            }
        }
        HeapFree(GetProcessHeap(), 0, Utf8);
    }
    BufferText(Buffer, "\"");
}

static VOID BufferField(_Inout_ PDIAGDATA_BUFFER Buffer, _In_ PCSTR Name)
{
    BufferText(Buffer, ",\"");
    BufferText(Buffer, Name);
    BufferText(Buffer, "\":");
}

static VOID TrimText(_Inout_ PWSTR Text)
{
    SIZE_T Start = 0;
    SIZE_T Length = wcslen(Text);

    while (Length != 0 && Text[Length - 1] == L' ')
        Text[--Length] = UNICODE_NULL;
    while (Text[Start] == L' ')
        Start++;
    if (Start != 0)
        MoveMemory(Text, Text + Start, (Length - Start + 1) * sizeof(WCHAR));
}

static BOOL ReadText(_In_ HKEY Key, _In_opt_ PCWSTR SubKey, _In_ PCWSTR Name, _Out_writes_(Length) PWSTR Text, _In_ DWORD Length)
{
    HKEY Opened = Key;
    DWORD Type = 0;
    DWORD Size = (Length - 1) * sizeof(WCHAR);
    LONG Error;

    Text[0] = UNICODE_NULL;
    if (SubKey != NULL && RegOpenKeyExW(Key, SubKey, 0, KEY_QUERY_VALUE, &Opened) != ERROR_SUCCESS)
        return FALSE;
    Error = RegQueryValueExW(Opened, Name, NULL, &Type, (LPBYTE)Text, &Size);
    if (Opened != Key)
        RegCloseKey(Opened);
    if (Error != ERROR_SUCCESS || (Type != REG_SZ && Type != REG_EXPAND_SZ))
    {
        Text[0] = UNICODE_NULL;
        return FALSE;
    }
    Text[min(Size / sizeof(WCHAR), Length - 1)] = UNICODE_NULL;
    TrimText(Text);
    return Text[0] != UNICODE_NULL;
}

static VOID LoadInstallId(VOID)
{
    UUID Id;
    RPC_WSTR Text;

    if (ReadText(DiagDataKey, NULL, L"InstallId", DiagDataState.InstallId, ARRAYSIZE(DiagDataState.InstallId)) &&
        wcslen(DiagDataState.InstallId) == 36)
    {
        return;
    }

    DiagDataState.InstallId[0] = UNICODE_NULL;
    if (UuidCreate(&Id) != RPC_S_OK || UuidToStringW(&Id, &Text) != RPC_S_OK)
        return;
    lstrcpynW(DiagDataState.InstallId, (PCWSTR)Text, ARRAYSIZE(DiagDataState.InstallId));
    RpcStringFreeW(&Text);
    RegSetValueExW(DiagDataKey, L"InstallId", 0, REG_SZ, (const BYTE *)DiagDataState.InstallId,
                   (DWORD)((wcslen(DiagDataState.InstallId) + 1) * sizeof(WCHAR)));
}

static VOID LoadProgress(VOID)
{
    DWORD Type = 0;
    DWORD Size = sizeof(DiagDataState.Reported.KernelCrash);

    if (RegQueryValueExW(DiagDataKey, L"ReportedKernelCrash", NULL, &Type, (LPBYTE)&DiagDataState.Reported.KernelCrash, &Size) != ERROR_SUCCESS ||
        Type != REG_QWORD)
    {
        DiagDataState.Reported.KernelCrash = 0;
    }
    Size = sizeof(DiagDataState.Reported.AppCrash);
    if (RegQueryValueExW(DiagDataKey, L"ReportedAppCrash", NULL, &Type, (LPBYTE)&DiagDataState.Reported.AppCrash, &Size) != ERROR_SUCCESS ||
        Type != REG_DWORD)
    {
        DiagDataState.Reported.AppCrash = 0;
    }
    Size = sizeof(DiagDataState.Reported.SystemEvent);
    if (RegQueryValueExW(DiagDataKey, L"ReportedSystemEvent", NULL, &Type, (LPBYTE)&DiagDataState.Reported.SystemEvent, &Size) != ERROR_SUCCESS ||
        Type != REG_DWORD)
    {
        DiagDataState.Reported.SystemEvent = 0;
    }
}

static VOID SaveProgress(_In_ const DIAGDATA_PROGRESS *Progress)
{
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, DIAGDATA_KEY, 0, KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
        return;
    RegSetValueExW(Key, L"ReportedKernelCrash", 0, REG_QWORD, (const BYTE *)&Progress->KernelCrash, sizeof(Progress->KernelCrash));
    RegSetValueExW(Key, L"ReportedAppCrash", 0, REG_DWORD, (const BYTE *)&Progress->AppCrash, sizeof(Progress->AppCrash));
    RegSetValueExW(Key, L"ReportedSystemEvent", 0, REG_DWORD, (const BYTE *)&Progress->SystemEvent, sizeof(Progress->SystemEvent));
    RegFlushKey(Key);
    RegCloseKey(Key);
}

static ULONGLONG ReadFileTime(_In_ HKEY Key, _In_opt_ PCWSTR SubKey, _In_ PCWSTR Name, _In_ DWORD ExpectedType)
{
    ULONGLONG Value = 0;
    HKEY Opened = Key;
    DWORD Type = 0;
    DWORD Size = sizeof(Value);

    if (SubKey != NULL && RegOpenKeyExW(Key, SubKey, 0, KEY_QUERY_VALUE, &Opened) != ERROR_SUCCESS)
        return 0;
    if (RegQueryValueExW(Opened, Name, NULL, &Type, (LPBYTE)&Value, &Size) != ERROR_SUCCESS || Type != ExpectedType || Size != sizeof(Value))
        Value = 0;
    if (Opened != Key)
        RegCloseKey(Opened);
    return Value;
}

static VOID OpenSession(VOID)
{
    ULARGE_INTEGER Now;
    FILETIME Current;
    ULONGLONG Started;
    ULONGLONG Shutdown;

    Started = ReadFileTime(DiagDataKey, NULL, L"SessionStart", REG_QWORD);
    Shutdown = ReadFileTime(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Windows", L"ShutdownTime", REG_BINARY);
    if (Started == 0)
        DiagDataState.PreviousShutdown = "unknown";
    else
        DiagDataState.PreviousShutdown = Shutdown > Started ? "clean" : "unexpected";

    GetSystemTimeAsFileTime(&Current);
    Now.LowPart = Current.dwLowDateTime;
    Now.HighPart = Current.dwHighDateTime;
    RegSetValueExW(DiagDataKey, L"SessionStart", 0, REG_QWORD, (const BYTE *)&Now.QuadPart, sizeof(Now.QuadPart));
    RegFlushKey(DiagDataKey);
}

static PCSTR ArchitectureName(_In_ WORD Architecture)
{
    switch (Architecture)
    {
        case PROCESSOR_ARCHITECTURE_INTEL: return "i386";
        case PROCESSOR_ARCHITECTURE_AMD64: return "amd64";
        case PROCESSOR_ARCHITECTURE_ARM: return "arm";
        case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
        case PROCESSOR_ARCHITECTURE_RISCV64: return "riscv64";
        case PROCESSOR_ARCHITECTURE_PPC: return "ppc";
        default: return "unknown";
    }
}

static PCSTR FirmwareName(VOID)
{
    FIRMWARE_TYPE Type;

    if (!GetFirmwareType(&Type))
        return "unknown";
    if (Type == FirmwareTypeUefi)
        return "uefi";
    if (Type == FirmwareTypeBios)
        return "bios";
    return "unknown";
}

static VOID SmbiosString(_In_ const BYTE *Strings, _In_ const BYTE *End, _In_ BYTE Index, _Out_writes_(Length) PWSTR Text, _In_ DWORD Length)
{
    const BYTE *Current = Strings;
    const BYTE *Stop;

    Text[0] = UNICODE_NULL;
    if (Index == 0)
        return;
    while (--Index != 0)
    {
        while (Current < End && *Current != 0)
            Current++;
        if (Current >= End || ++Current >= End || *Current == 0)
            return;
    }
    for (Stop = Current; Stop < End && *Stop != 0; Stop++);
    if (Stop == Current)
        return;
    Length = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)Current, (INT)(Stop - Current), Text, Length - 1);
    Text[Length] = UNICODE_NULL;
    TrimText(Text);
}

static VOID ReadSystemModel(_Out_writes_(DIAGDATA_TEXT_LENGTH) PWSTR Manufacturer, _Out_writes_(DIAGDATA_TEXT_LENGTH) PWSTR Product)
{
    PBYTE Table;
    const BYTE *Entry;
    const BYTE *End;
    const BYTE *Strings;
    DWORD Size;
    DWORD TableLength;

    Manufacturer[0] = UNICODE_NULL;
    Product[0] = UNICODE_NULL;
    Size = GetSystemFirmwareTable('RSMB', 0, NULL, 0);
    if (Size <= DIAGDATA_SMBIOS_HEADER)
        return;
    Table = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Table == NULL)
        return;
    if (GetSystemFirmwareTable('RSMB', 0, Table, Size) != Size)
        goto Exit;

    CopyMemory(&TableLength, Table + 4, sizeof(TableLength));
    if (TableLength > Size - DIAGDATA_SMBIOS_HEADER)
        goto Exit;
    Entry = Table + DIAGDATA_SMBIOS_HEADER;
    End = Entry + TableLength;
    while (End - Entry >= 4 && Entry[1] >= 4 && Entry[1] <= End - Entry)
    {
        Strings = Entry + Entry[1];
        if (Entry[0] == DIAGDATA_SMBIOS_SYSTEM && Entry[1] >= 6)
        {
            SmbiosString(Strings, End, Entry[4], Manufacturer, DIAGDATA_TEXT_LENGTH);
            SmbiosString(Strings, End, Entry[5], Product, DIAGDATA_TEXT_LENGTH);
            break;
        }
        if (Entry[0] == DIAGDATA_SMBIOS_END)
            break;
        while (End - Strings >= 2 && (Strings[0] != 0 || Strings[1] != 0))
            Strings++;
        if (End - Strings < 2)
            break;
        Entry = Strings + 2;
    }

Exit:
    HeapFree(GetProcessHeap(), 0, Table);
}

static ULONG AppendDevices(_Inout_ PDIAGDATA_BUFFER Buffer, _In_ PCWSTR Enumerator, _In_ ULONG Count)
{
    HDEVINFO Devices;
    SP_DEVINFO_DATA Device;
    WCHAR HardwareIds[1024];
    WCHAR Service[DIAGDATA_TEXT_LENGTH];
    ULONG Status;
    ULONG Problem;
    DWORD Index;

    Devices = SetupDiGetClassDevsW(NULL, Enumerator, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (Devices == INVALID_HANDLE_VALUE)
        return Count;

    Device.cbSize = sizeof(Device);
    for (Index = 0; Count < DIAGDATA_MAX_DEVICES && SetupDiEnumDeviceInfo(Devices, Index, &Device); Index++)
    {
        ZeroMemory(HardwareIds, sizeof(HardwareIds));
        if (!SetupDiGetDeviceRegistryPropertyW(Devices, &Device, SPDRP_HARDWAREID, NULL, (PBYTE)HardwareIds,
                                               sizeof(HardwareIds) - 2 * sizeof(WCHAR), NULL) || HardwareIds[0] == UNICODE_NULL)
        {
            continue;
        }

        ZeroMemory(Service, sizeof(Service));
        if (!SetupDiGetDeviceRegistryPropertyW(Devices, &Device, SPDRP_SERVICE, NULL, (PBYTE)Service, sizeof(Service) - sizeof(WCHAR), NULL))
            Service[0] = UNICODE_NULL;

        if (CM_Get_DevNode_Status(&Status, &Problem, Device.DevInst, 0) != CR_SUCCESS || !(Status & DN_HAS_PROBLEM))
            Problem = 0;

        BufferText(Buffer, Count != 0 ? ",{\"id\":" : "{\"id\":");
        BufferString(Buffer, HardwareIds);
        BufferText(Buffer, ",\"service\":");
        BufferString(Buffer, Service);
        BufferText(Buffer, ",\"problem\":");
        BufferNumber(Buffer, Problem);
        BufferText(Buffer, "}");
        Count++;
    }

    SetupDiDestroyDeviceInfoList(Devices);
    return Count;
}

static VOID BufferHex(_Inout_ PDIAGDATA_BUFFER Buffer, _In_ ULONGLONG Value)
{
    CHAR Text[24];

    _snprintf(Text, ARRAYSIZE(Text), "\"%I64x\"", Value);
    Text[ARRAYSIZE(Text) - 1] = ANSI_NULL;
    BufferText(Buffer, Text);
}

static VOID AppendKernelCrash(_Inout_ PDIAGDATA_BUFFER Buffer, _Inout_ PDIAGDATA_PROGRESS Progress)
{
    PDUMP_HEADER64 Header64;
    PDUMP_HEADER32 Header32;
    WCHAR Setting[MAX_PATH];
    WCHAR Path[MAX_PATH];
    ULONGLONG Parameters[4] = {0};
    ULARGE_INTEGER Saved = {0};
    FILETIME Written;
    HANDLE File;
    DWORD Read;
    ULONG Code = 0;
    ULONG Index;
    BOOL Found = FALSE;

    BufferField(Buffer, "kernel_crash");
    Header64 = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Header64));
    Header32 = (PDUMP_HEADER32)Header64;
    if (!ReadText(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\CrashControl", L"DumpFile", Setting, ARRAYSIZE(Setting)))
        lstrcpynW(Setting, L"%SystemRoot%\\MEMORY.DMP", ARRAYSIZE(Setting));
    Read = ExpandEnvironmentStringsW(Setting, Path, ARRAYSIZE(Path));
    File = (Header64 != NULL && Read != 0 && Read <= ARRAYSIZE(Path)) ?
           CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL) :
           INVALID_HANDLE_VALUE;
    if (File != INVALID_HANDLE_VALUE)
    {
        if (GetFileTime(File, NULL, NULL, &Written) && ReadFile(File, Header64, FIELD_OFFSET(DUMP_HEADER64, VersionUser), &Read, NULL) &&
            Read == FIELD_OFFSET(DUMP_HEADER64, VersionUser))
        {
            Saved.LowPart = Written.dwLowDateTime;
            Saved.HighPart = Written.dwHighDateTime;
            if (Saved.QuadPart > Progress->KernelCrash && Header64->Signature == DUMP_SIGNATURE64 && Header64->ValidDump == DUMP_VALID_DUMP64)
            {
                Code = Header64->BugCheckCode;
                Parameters[0] = Header64->BugCheckParameter1;
                Parameters[1] = Header64->BugCheckParameter2;
                Parameters[2] = Header64->BugCheckParameter3;
                Parameters[3] = Header64->BugCheckParameter4;
                Found = TRUE;
            }
            else if (Saved.QuadPart > Progress->KernelCrash && Header32->Signature == DUMP_SIGNATURE32 && Header32->ValidDump == DUMP_VALID_DUMP32)
            {
                Code = Header32->BugCheckCode;
                Parameters[0] = Header32->BugCheckParameter1;
                Parameters[1] = Header32->BugCheckParameter2;
                Parameters[2] = Header32->BugCheckParameter3;
                Parameters[3] = Header32->BugCheckParameter4;
                Found = TRUE;
            }
        }
        CloseHandle(File);
    }
    if (Header64 != NULL)
        HeapFree(GetProcessHeap(), 0, Header64);

    if (!Found || Saved.QuadPart < DIAGDATA_UNIX_EPOCH)
    {
        BufferText(Buffer, "null");
        return;
    }

    Progress->KernelCrash = Saved.QuadPart;
    BufferText(Buffer, "{\"time\":");
    BufferNumber(Buffer, (Saved.QuadPart - DIAGDATA_UNIX_EPOCH) / DIAGDATA_FILETIME_PER_SECOND);
    BufferText(Buffer, ",\"bugcheck\":");
    BufferNumber(Buffer, Code);
    BufferText(Buffer, ",\"parameters\":[");
    for (Index = 0; Index < ARRAYSIZE(Parameters); Index++)
    {
        if (Index != 0)
            BufferText(Buffer, ",");
        BufferHex(Buffer, Parameters[Index]);
    }
    BufferText(Buffer, "]}");
}

static BOOL ReadApplicationError(_In_ const EVENTLOGRECORD *Record, _Out_writes_(DIAGDATA_APPLICATION_ERROR_STRINGS) PCWSTR *Strings)
{
    PCWSTR Source = (PCWSTR)(Record + 1);
    PCWSTR End = (PCWSTR)((const BYTE *)Record + Record->Length);
    PCWSTR Text;
    ULONG Index;

    if (LOWORD(Record->EventID) != DIAGDATA_APPLICATION_ERROR || Record->NumStrings < DIAGDATA_APPLICATION_ERROR_STRINGS ||
        Record->StringOffset >= Record->Length || _wcsicmp(Source, L"Application Error") != 0)
    {
        return FALSE;
    }

    Text = (PCWSTR)((const BYTE *)Record + Record->StringOffset);
    for (Index = 0; Index < DIAGDATA_APPLICATION_ERROR_STRINGS; Index++)
    {
        Strings[Index] = Text;
        while (Text < End && *Text != UNICODE_NULL)
            Text++;
        if (Text >= End)
            return FALSE;
        Text++;
    }
    return TRUE;
}

static VOID AppendAppCrashes(_Inout_ PDIAGDATA_BUFFER Buffer, _Inout_ PDIAGDATA_PROGRESS Progress)
{
    PCWSTR Strings[DIAGDATA_APPLICATION_ERROR_STRINGS];
    const EVENTLOGRECORD *Record;
    HANDLE Log;
    PBYTE Records;
    DWORD Oldest;
    DWORD Count;
    DWORD Next;
    DWORD Read;
    DWORD Needed;
    DWORD Offset;
    ULONG Crashes = 0;

    BufferField(Buffer, "app_crashes");
    BufferText(Buffer, "[");
    Log = OpenEventLogW(NULL, L"Application");
    Records = HeapAlloc(GetProcessHeap(), 0, DIAGDATA_EVENT_BUFFER);
    if (Log == NULL || Records == NULL || !GetOldestEventLogRecord(Log, &Oldest) || !GetNumberOfEventLogRecords(Log, &Count) || Count == 0)
        goto Exit;

    Next = Progress->AppCrash + 1;
    if (Next < Oldest || Next > Oldest + Count)
        Next = Oldest;

    while (Crashes < DIAGDATA_MAX_APP_CRASHES && Next < Oldest + Count &&
           ReadEventLogW(Log, EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, Next, Records, DIAGDATA_EVENT_BUFFER, &Read, &Needed))
    {
        for (Offset = 0; Crashes < DIAGDATA_MAX_APP_CRASHES && Offset + sizeof(EVENTLOGRECORD) <= Read; Offset += Record->Length)
        {
            Record = (const EVENTLOGRECORD *)(Records + Offset);
            if (Record->Length < sizeof(EVENTLOGRECORD) || Record->Length > Read - Offset)
            {
                Next = Oldest + Count;
                break;
            }
            Next = Record->RecordNumber + 1;
            Progress->AppCrash = Record->RecordNumber;
            if (!ReadApplicationError(Record, Strings))
                continue;

            BufferText(Buffer, Crashes != 0 ? ",{\"time\":" : "{\"time\":");
            BufferNumber(Buffer, Record->TimeGenerated);
            BufferText(Buffer, ",\"application\":");
            BufferString(Buffer, Strings[0]);
            BufferText(Buffer, ",\"application_version\":");
            BufferString(Buffer, Strings[1]);
            BufferText(Buffer, ",\"module\":");
            BufferString(Buffer, Strings[3]);
            BufferText(Buffer, ",\"module_version\":");
            BufferString(Buffer, Strings[4]);
            BufferText(Buffer, ",\"code\":");
            BufferString(Buffer, Strings[6]);
            BufferText(Buffer, ",\"offset\":");
            BufferString(Buffer, Strings[7]);
            BufferText(Buffer, "}");
            Crashes++;
        }
    }

Exit:
    BufferText(Buffer, "]");
    if (Records != NULL)
        HeapFree(GetProcessHeap(), 0, Records);
    if (Log != NULL)
        CloseEventLog(Log);
}

static BOOL BuildReport(_Out_ PDIAGDATA_BUFFER Buffer, _Out_ PDIAGDATA_PROGRESS Progress)
{
    static const PCWSTR Enumerators[] = {L"ACPI", L"PCI", L"USB"};
    SYSTEM_INFO System;
    MEMORYSTATUSEX Memory;
    WCHAR Text[DIAGDATA_TEXT_LENGTH];
    WCHAR Product[DIAGDATA_TEXT_LENGTH];
    WCHAR ReportId[40];
    ULARGE_INTEGER Created;
    FILETIME CreatedTime;
    RPC_WSTR ReportIdText;
    UUID ReportUuid;
    ULONGLONG MemoryMegabytes;
    ULONG Devices = 0;
    ULONG Index;

    ZeroMemory(Buffer, sizeof(*Buffer));
    *Progress = DiagDataState.Reported;
    GetNativeSystemInfo(&System);
    Memory.dwLength = sizeof(Memory);
    MemoryMegabytes = GlobalMemoryStatusEx(&Memory) ? Memory.ullTotalPhys / (1024 * 1024) : 0;
    MemoryMegabytes = ((MemoryMegabytes + DIAGDATA_MEMORY_GRANULE_MB / 2) / DIAGDATA_MEMORY_GRANULE_MB) * DIAGDATA_MEMORY_GRANULE_MB;

    ReportId[0] = UNICODE_NULL;
    if (UuidCreate(&ReportUuid) == RPC_S_OK && UuidToStringW(&ReportUuid, &ReportIdText) == RPC_S_OK)
    {
        lstrcpynW(ReportId, (PCWSTR)ReportIdText, ARRAYSIZE(ReportId));
        RpcStringFreeW(&ReportIdText);
    }
    GetSystemTimeAsFileTime(&CreatedTime);
    Created.LowPart = CreatedTime.dwLowDateTime;
    Created.HighPart = CreatedTime.dwHighDateTime;

    BufferText(Buffer, "{\"schema\":1");
    BufferField(Buffer, "report_id");
    BufferString(Buffer, ReportId);
    BufferField(Buffer, "created");
    BufferNumber(Buffer, Created.QuadPart >= DIAGDATA_UNIX_EPOCH ? (Created.QuadPart - DIAGDATA_UNIX_EPOCH) / DIAGDATA_FILETIME_PER_SECOND : 0);
    BufferField(Buffer, "install_id");
    BufferString(Buffer, DiagDataState.InstallId);
    BufferField(Buffer, "channel");
    BufferString(Buffer, DiagDataState.Channel);
    ReadText(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"BuildLab", Text, ARRAYSIZE(Text));
    BufferField(Buffer, "build");
    BufferString(Buffer, Text);
    BufferField(Buffer, "architecture");
    BufferText(Buffer, "\"");
    BufferText(Buffer, ArchitectureName(System.wProcessorArchitecture));
    BufferText(Buffer, "\"");
    BufferField(Buffer, "firmware");
    BufferText(Buffer, "\"");
    BufferText(Buffer, FirmwareName());
    BufferText(Buffer, "\"");
    BufferField(Buffer, "boot_ms");
    BufferNumber(Buffer, DiagDataState.BootMilliseconds);
    BufferField(Buffer, "previous_shutdown");
    BufferText(Buffer, "\"");
    BufferText(Buffer, DiagDataState.PreviousShutdown);
    BufferText(Buffer, "\"");
    ReadText(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", Text, ARRAYSIZE(Text));
    BufferField(Buffer, "cpu");
    BufferString(Buffer, Text);
    BufferField(Buffer, "processors");
    BufferNumber(Buffer, System.dwNumberOfProcessors);
    BufferField(Buffer, "memory_mb");
    BufferNumber(Buffer, MemoryMegabytes);
    ReadSystemModel(Text, Product);
    BufferField(Buffer, "manufacturer");
    BufferString(Buffer, Text);
    BufferField(Buffer, "product");
    BufferString(Buffer, Product);
    BufferField(Buffer, "devices");
    BufferText(Buffer, "[");
    for (Index = 0; Index < ARRAYSIZE(Enumerators); Index++)
        Devices = AppendDevices(Buffer, Enumerators[Index], Devices);
    BufferText(Buffer, "]");
    AppendKernelCrash(Buffer, Progress);
    AppendAppCrashes(Buffer, Progress);
    BufferText(Buffer, "}");

    return !Buffer->Failed;
}

static DWORD DiagDataLevel(VOID)
{
    HKEY Key;
    DWORD Level = 1;
    DWORD Type = 0;
    DWORD Size = sizeof(Level);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, DIAGDATA_DATACOLLECTION_KEY, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return 1;
    if (RegQueryValueExW(Key, L"AllowTelemetry", NULL, &Type, (LPBYTE)&Level, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        Level = 1;
    RegCloseKey(Key);
    return Level;
}

typedef struct _DIAGDATA_EVENT_GROUP
{
    WCHAR Source[64];
    DWORD EventId;
    WORD EventType;
    DWORD Count;
} DIAGDATA_EVENT_GROUP;

static VOID BufferOptional(_Inout_ PDIAGDATA_BUFFER Buffer, _Inout_ PDIAGDATA_PROGRESS Progress)
{
    DIAGDATA_EVENT_GROUP *Groups;
    const EVENTLOGRECORD *Record;
    PCWSTR Source;
    HANDLE Log;
    PBYTE Records;
    DWORD Oldest = 0;
    DWORD Count = 0;
    DWORD Next;
    DWORD Read;
    DWORD Needed;
    DWORD Offset;
    ULONG GroupCount = 0;
    ULONG Index;

    BufferText(Buffer, ",\"optional\":{\"events\":[");
    Log = OpenEventLogW(NULL, L"System");
    Records = HeapAlloc(GetProcessHeap(), 0, DIAGDATA_EVENT_BUFFER);
    Groups = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, DIAGDATA_MAX_EVENTS * sizeof(*Groups));
    if (Log == NULL || Records == NULL || Groups == NULL ||
        !GetOldestEventLogRecord(Log, &Oldest) || !GetNumberOfEventLogRecords(Log, &Count) || Count == 0)
    {
        goto Emit;
    }

    Next = Progress->SystemEvent + 1;
    if (Next < Oldest || Next > Oldest + Count)
        Next = Oldest;

    while (Next < Oldest + Count &&
           ReadEventLogW(Log, EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, Next, Records, DIAGDATA_EVENT_BUFFER, &Read, &Needed))
    {
        for (Offset = 0; Offset + sizeof(EVENTLOGRECORD) <= Read; Offset += Record->Length)
        {
            Record = (const EVENTLOGRECORD *)(Records + Offset);
            if (Record->Length < sizeof(EVENTLOGRECORD) || Record->Length > Read - Offset)
            {
                Next = Oldest + Count;
                break;
            }
            Next = Record->RecordNumber + 1;
            Progress->SystemEvent = Record->RecordNumber;
            if (Record->EventType != EVENTLOG_ERROR_TYPE)
                continue;

            Source = (PCWSTR)(Record + 1);
            for (Index = 0; Index < GroupCount; Index++)
            {
                if (Groups[Index].EventId == LOWORD(Record->EventID) && Groups[Index].EventType == Record->EventType &&
                    _wcsicmp(Groups[Index].Source, Source) == 0)
                {
                    break;
                }
            }
            if (Index == GroupCount)
            {
                if (GroupCount >= DIAGDATA_MAX_EVENTS)
                    continue;
                lstrcpynW(Groups[Index].Source, Source, ARRAYSIZE(Groups[Index].Source));
                Groups[Index].EventId = LOWORD(Record->EventID);
                Groups[Index].EventType = Record->EventType;
                GroupCount++;
            }
            Groups[Index].Count++;
        }
    }

Emit:
    for (Index = 0; Index < GroupCount; Index++)
    {
        BufferText(Buffer, Index != 0 ? ",{\"source\":" : "{\"source\":");
        BufferString(Buffer, Groups[Index].Source);
        BufferText(Buffer, ",\"event\":");
        BufferNumber(Buffer, Groups[Index].EventId);
        BufferText(Buffer, ",\"type\":");
        BufferNumber(Buffer, Groups[Index].EventType);
        BufferText(Buffer, ",\"count\":");
        BufferNumber(Buffer, Groups[Index].Count);
        BufferText(Buffer, "}");
    }
    BufferText(Buffer, "]}");
    if (Records != NULL)
        HeapFree(GetProcessHeap(), 0, Records);
    if (Groups != NULL)
        HeapFree(GetProcessHeap(), 0, Groups);
    if (Log != NULL)
        CloseEventLog(Log);
}

static BOOL BuildOptionalBody(_In_ const DIAGDATA_BUFFER *Required, _Out_ PDIAGDATA_BUFFER Out, _Inout_ PDIAGDATA_PROGRESS Progress)
{
    ZeroMemory(Out, sizeof(*Out));
    if (Required->Length == 0 || Required->Data[Required->Length - 1] != '}')
        return FALSE;
    if (!BufferReserve(Out, Required->Length))
        return FALSE;
    CopyMemory(Out->Data, Required->Data, Required->Length - 1);
    Out->Length = Required->Length - 1;
    BufferOptional(Out, Progress);
    BufferText(Out, "}");
    return !Out->Failed;
}

static DWORD SendBody(_In_reads_bytes_(Length) const void *Data, _In_ DWORD Length, _Out_ PDWORD StatusCode, _Out_ PDWORD RetryAfterMs)
{
    URL_COMPONENTS Url;
    WCHAR Host[DIAGDATA_TEXT_LENGTH];
    WCHAR Path[DIAGDATA_URL_LENGTH];
    WCHAR RetryAfter[64];
    HINTERNET Session = NULL;
    HINTERNET Connection = NULL;
    HINTERNET Request = NULL;
    DWORD Size = sizeof(*StatusCode);
    DWORD RetrySize = sizeof(RetryAfter);
    DWORD Seconds;
    DWORD Error = NO_ERROR;

    *StatusCode = 0;
    *RetryAfterMs = 0;
    ZeroMemory(&Url, sizeof(Url));
    Url.dwStructSize = sizeof(Url);
    Url.lpszHostName = Host;
    Url.dwHostNameLength = ARRAYSIZE(Host);
    Url.lpszUrlPath = Path;
    Url.dwUrlPathLength = ARRAYSIZE(Path);
    if (!WinHttpCrackUrl(DiagDataState.Endpoint, 0, 0, &Url))
        return GetLastError();

    Session = WinHttpOpen(DIAGDATA_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (Session == NULL)
        return GetLastError();
    WinHttpSetTimeouts(Session, DIAGDATA_HTTP_TIMEOUT_MS, DIAGDATA_HTTP_TIMEOUT_MS, DIAGDATA_HTTP_TIMEOUT_MS, DIAGDATA_HTTP_TIMEOUT_MS);

    Connection = WinHttpConnect(Session, Host, Url.nPort, 0);
    if (Connection != NULL)
    {
        Request = WinHttpOpenRequest(Connection, L"POST", Path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     Url.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    }
    if (Request == NULL ||
        !WinHttpSendRequest(Request, L"Content-Type: application/json\r\n", (DWORD)-1, (LPVOID)Data, Length, Length, 0) ||
        !WinHttpReceiveResponse(Request, NULL) ||
        !WinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             StatusCode, &Size, WINHTTP_NO_HEADER_INDEX))
    {
        Error = GetLastError();
    }
    else if (WinHttpQueryHeaders(Request, WINHTTP_QUERY_RETRY_AFTER, WINHTTP_HEADER_NAME_BY_INDEX, RetryAfter, &RetrySize, WINHTTP_NO_HEADER_INDEX))
    {
        Seconds = (DWORD)wcstoul(RetryAfter, NULL, 10);
        if (Seconds != 0)
            *RetryAfterMs = min(Seconds * 1000, DIAGDATA_RETRY_CAP_MS);
    }

    if (Request != NULL)
        WinHttpCloseHandle(Request);
    if (Connection != NULL)
        WinHttpCloseHandle(Connection);
    WinHttpCloseHandle(Session);
    return Error;
}

static BOOL SendSucceeded(_In_ DWORD Error, _In_ DWORD StatusCode)
{
    return Error == NO_ERROR && StatusCode >= 200 && StatusCode < 300;
}

static BOOL SendPermanentDrop(_In_ DWORD Error, _In_ DWORD StatusCode)
{
    return Error == NO_ERROR && (StatusCode == 400 || StatusCode == 413 || StatusCode == 422);
}

static DWORD NewestSystemRecord(_In_ DWORD Current)
{
    HANDLE Log;
    DWORD Oldest = 0;
    DWORD Count = 0;
    DWORD Newest = Current;

    Log = OpenEventLogW(NULL, L"System");
    if (Log != NULL)
    {
        if (GetOldestEventLogRecord(Log, &Oldest) && GetNumberOfEventLogRecords(Log, &Count) && Count != 0 && Oldest + Count - 1 > Newest)
            Newest = Oldest + Count - 1;
        CloseEventLog(Log);
    }
    return Newest;
}

static BOOL SpoolDirectory(_Out_writes_(Length) PWSTR Path, _In_ DWORD Length)
{
    WCHAR Pattern[MAX_PATH];
    DWORD Produced;

    if (FAILED(StringCchCopyW(Pattern, ARRAYSIZE(Pattern), L"%SystemRoot%")) ||
        FAILED(StringCchCatW(Pattern, ARRAYSIZE(Pattern), DIAGDATA_SPOOL_SUBDIR)))
    {
        return FALSE;
    }
    Produced = ExpandEnvironmentStringsW(Pattern, Path, Length);
    return Produced != 0 && Produced <= Length;
}

static BOOL SpoolEnsureDirectory(_In_ PCWSTR SpoolPath)
{
    SECURITY_ATTRIBUTES Security;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    WCHAR Parent[MAX_PATH];
    PWSTR LastSlash;
    BOOL Ok = FALSE;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(DIAGDATA_SPOOL_DACL, SDDL_REVISION_1, &Descriptor, NULL))
        return FALSE;
    Security.nLength = sizeof(Security);
    Security.lpSecurityDescriptor = Descriptor;
    Security.bInheritHandle = FALSE;

    if (SUCCEEDED(StringCchCopyW(Parent, ARRAYSIZE(Parent), SpoolPath)) && (LastSlash = wcsrchr(Parent, L'\\')) != NULL)
    {
        *LastSlash = UNICODE_NULL;
        if (!CreateDirectoryW(Parent, &Security) && GetLastError() != ERROR_ALREADY_EXISTS)
            goto Exit;
    }
    Ok = CreateDirectoryW(SpoolPath, &Security) || GetLastError() == ERROR_ALREADY_EXISTS;

Exit:
    LocalFree(Descriptor);
    return Ok;
}

static ULONG SpoolList(_In_ PCWSTR SpoolPath, _Out_writes_(Capacity) PWSTR *Names, _In_ ULONG Capacity, _Out_ PULONGLONG TotalBytes)
{
    WIN32_FIND_DATAW Find;
    WCHAR Pattern[MAX_PATH];
    HANDLE Search;
    PWSTR Name;
    ULONG Count = 0;
    ULONG Index;

    *TotalBytes = 0;
    if (FAILED(StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*.json", SpoolPath)))
        return 0;
    Search = FindFirstFileW(Pattern, &Find);
    if (Search == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (Find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (Count >= Capacity)
            continue;
        Name = HeapAlloc(GetProcessHeap(), 0, (wcslen(Find.cFileName) + 1) * sizeof(WCHAR));
        if (Name == NULL)
            continue;
        lstrcpyW(Name, Find.cFileName);
        *TotalBytes += Find.nFileSizeLow;
        for (Index = Count; Index > 0 && wcscmp(Names[Index - 1], Name) > 0; Index--)
            Names[Index] = Names[Index - 1];
        Names[Index] = Name;
        Count++;
    } while (FindNextFileW(Search, &Find));
    FindClose(Search);
    return Count;
}

static BOOL SpoolWrite(_In_reads_bytes_(Length) const void *Data, _In_ DWORD Length, _Out_writes_(NameSize) PWSTR Name, _In_ DWORD NameSize)
{
    WCHAR SpoolPath[MAX_PATH];
    WCHAR FilePath[MAX_PATH];
    WCHAR TempPath[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA Attributes;
    PWSTR Names[128];
    FILETIME Now;
    ULARGE_INTEGER Stamp;
    ULONGLONG TotalBytes;
    HANDLE File;
    DWORD Written;
    ULONG Count;
    ULONG Index;
    BOOL Ok = FALSE;

    Name[0] = UNICODE_NULL;
    if (!SpoolDirectory(SpoolPath, ARRAYSIZE(SpoolPath)) || !SpoolEnsureDirectory(SpoolPath))
    {
        DPRINT1("DIAGDATA: spool directory unavailable (error %lu)\n", GetLastError());
        return FALSE;
    }

    Count = SpoolList(SpoolPath, Names, ARRAYSIZE(Names), &TotalBytes);
    for (Index = 0; Index < Count; Index++)
    {
        if ((Count - Index) + 1 <= DIAGDATA_SPOOL_MAX_FILES && TotalBytes + Length <= DIAGDATA_SPOOL_MAX_BYTES)
            break;
        if (SUCCEEDED(StringCchPrintfW(FilePath, ARRAYSIZE(FilePath), L"%s\\%s", SpoolPath, Names[Index])))
        {
            if (GetFileAttributesExW(FilePath, GetFileExInfoStandard, &Attributes))
                TotalBytes -= min(TotalBytes, Attributes.nFileSizeLow);
            DeleteFileW(FilePath);
        }
    }
    for (Index = 0; Index < Count; Index++)
        HeapFree(GetProcessHeap(), 0, Names[Index]);

    GetSystemTimeAsFileTime(&Now);
    Stamp.LowPart = Now.dwLowDateTime;
    Stamp.HighPart = Now.dwHighDateTime;
    if (FAILED(StringCchPrintfW(TempPath, ARRAYSIZE(TempPath), L"%s\\%016I64x.tmp", SpoolPath, Stamp.QuadPart)) ||
        FAILED(StringCchPrintfW(FilePath, ARRAYSIZE(FilePath), L"%s\\%016I64x.json", SpoolPath, Stamp.QuadPart)))
    {
        return FALSE;
    }

    File = CreateFileW(TempPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        DPRINT1("DIAGDATA: spool create failed (error %lu)\n", GetLastError());
        return FALSE;
    }
    if (WriteFile(File, Data, Length, &Written, NULL) && Written == Length && FlushFileBuffers(File))
        Ok = TRUE;
    CloseHandle(File);
    if (Ok && MoveFileExW(TempPath, FilePath, MOVEFILE_REPLACE_EXISTING))
    {
        StringCchPrintfW(Name, NameSize, L"%016I64x.json", Stamp.QuadPart);
        return TRUE;
    }
    DPRINT1("DIAGDATA: spool write failed (error %lu)\n", GetLastError());
    DeleteFileW(TempPath);
    return FALSE;
}

static VOID WaitForDevices(VOID)
{
    ULONGLONG Deadline = GetTickCount64() + DIAGDATA_SETTLE_MS;
    ULONGLONG Now;

    while ((Now = GetTickCount64()) < Deadline)
    {
        if (CMP_WaitNoPendingInstallEvents((DWORD)(Deadline - Now)) != WAIT_FAILED)
            return;
        if (WaitForSingleObject(StopEvent, DIAGDATA_PNP_POLL_MS) != WAIT_TIMEOUT)
            return;
    }
}

static VOID SpoolDrain(_In_opt_ const DIAGDATA_BUFFER *Required, _In_opt_ PCWSTR CurrentName, _In_opt_ const DIAGDATA_PROGRESS *CrashProgress)
{
    WCHAR SpoolPath[MAX_PATH];
    WCHAR FilePath[MAX_PATH];
    PWSTR Names[128];
    DIAGDATA_BUFFER Optional = {0};
    DIAGDATA_PROGRESS OptionalProgress = {0};
    HANDLE File;
    LARGE_INTEGER FileSize;
    ULONGLONG TotalBytes;
    PVOID Body;
    const void *Data;
    DWORD Length;
    DWORD Status;
    DWORD RetryAfter;
    DWORD Error;
    DWORD Read;
    ULONG Count;
    ULONG Index;
    DWORD Tries;
    BOOL HaveOptional;
    BOOL SentOptional;

    if (!SpoolDirectory(SpoolPath, ARRAYSIZE(SpoolPath)))
        return;
    Count = SpoolList(SpoolPath, Names, ARRAYSIZE(Names), &TotalBytes);
    for (Index = 0; Index < Count; Index++)
    {
        if (WaitForSingleObject(StopEvent, 0) != WAIT_TIMEOUT)
            break;
        if (FAILED(StringCchPrintfW(FilePath, ARRAYSIZE(FilePath), L"%s\\%s", SpoolPath, Names[Index])))
            continue;

        Body = NULL;
        HaveOptional = FALSE;
        SentOptional = FALSE;
        Data = NULL;
        Length = 0;

        if (Required != NULL && CurrentName != NULL && wcscmp(Names[Index], CurrentName) == 0)
        {
            if (DiagDataLevel() >= DIAGDATA_OPTIONAL_LEVEL)
            {
                OptionalProgress = *CrashProgress;
                HaveOptional = BuildOptionalBody(Required, &Optional, &OptionalProgress);
            }
            if (HaveOptional)
            {
                Data = Optional.Data;
                Length = (DWORD)Optional.Length;
                SentOptional = TRUE;
            }
            else
            {
                Data = Required->Data;
                Length = (DWORD)Required->Length;
            }
        }
        else
        {
            File = CreateFileW(FilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (File == INVALID_HANDLE_VALUE)
                continue;
            if (!GetFileSizeEx(File, &FileSize) || FileSize.QuadPart == 0 || FileSize.QuadPart > DIAGDATA_SPOOL_MAX_BYTES)
            {
                CloseHandle(File);
                DeleteFileW(FilePath);
                continue;
            }
            Body = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)FileSize.QuadPart);
            if (Body == NULL || !ReadFile(File, Body, (DWORD)FileSize.QuadPart, &Read, NULL) || Read != FileSize.QuadPart)
            {
                CloseHandle(File);
                if (Body != NULL)
                    HeapFree(GetProcessHeap(), 0, Body);
                continue;
            }
            CloseHandle(File);
            Data = Body;
            Length = Read;
        }

        Tries = 1;
        Error = SendBody(Data, Length, &Status, &RetryAfter);
        while (Tries < DIAGDATA_SEND_TRIES && !SendSucceeded(Error, Status) && !SendPermanentDrop(Error, Status))
        {
            if (WaitForSingleObject(StopEvent, DIAGDATA_SEND_WAIT_MS) != WAIT_TIMEOUT)
                break;
            Error = SendBody(Data, Length, &Status, &RetryAfter);
            Tries++;
        }
        DPRINT1("DIAGDATA: drain %S tries %lu error %lu status %lu optional %d\n", Names[Index], Tries, Error, Status, SentOptional);
        if (HaveOptional && Optional.Data != NULL)
            HeapFree(GetProcessHeap(), 0, Optional.Data);
        if (Body != NULL)
            HeapFree(GetProcessHeap(), 0, Body);

        if (SendSucceeded(Error, Status))
        {
            DeleteFileW(FilePath);
            if (SentOptional)
            {
                SaveProgress(&OptionalProgress);
                DiagDataState.Reported = OptionalProgress;
            }
        }
        else if (SendPermanentDrop(Error, Status))
        {
            DeleteFileW(FilePath);
        }
        else
        {
            break;
        }
    }
    for (Index = 0; Index < Count; Index++)
        HeapFree(GetProcessHeap(), 0, Names[Index]);
}

static DWORD WINAPI ReportThread(_In_ LPVOID Parameter)
{
    DIAGDATA_PROGRESS Progress;
    DIAGDATA_BUFFER Buffer;
    WCHAR Name[48];

    UNREFERENCED_PARAMETER(Parameter);

    WaitForDevices();

    if (!BuildReport(&Buffer, &Progress))
    {
        DPRINT1("DIAGDATA: could not build the report\n");
        if (Buffer.Data != NULL)
            HeapFree(GetProcessHeap(), 0, Buffer.Data);
        return 0;
    }

    if (SpoolWrite(Buffer.Data, (DWORD)Buffer.Length, Name, ARRAYSIZE(Name)))
    {
        if (DiagDataLevel() < DIAGDATA_OPTIONAL_LEVEL)
            Progress.SystemEvent = NewestSystemRecord(Progress.SystemEvent);
        SaveProgress(&Progress);
        DiagDataState.Reported = Progress;
        SpoolDrain(&Buffer, Name, &Progress);
    }
    HeapFree(GetProcessHeap(), 0, Buffer.Data);
    return 0;
}

static VOID UpdateServiceStatus(_In_ DWORD State)
{
    ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ServiceStatus.dwCurrentState = State;
    ServiceStatus.dwControlsAccepted = State == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
    ServiceStatus.dwWin32ExitCode = NO_ERROR;
    ServiceStatus.dwServiceSpecificExitCode = 0;
    ServiceStatus.dwCheckPoint = 0;
    ServiceStatus.dwWaitHint = 0;
    SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
}

static DWORD WINAPI ServiceControlHandler(_In_ DWORD Control, _In_ DWORD EventType, _In_opt_ LPVOID EventData, _In_opt_ LPVOID Context)
{
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(EventData);
    UNREFERENCED_PARAMETER(Context);

    switch (Control)
    {
        case SERVICE_CONTROL_STOP:
            UpdateServiceStatus(SERVICE_STOP_PENDING);
            SetEvent(StopEvent);
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
            return NO_ERROR;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static VOID WINAPI ServiceMain(_In_ DWORD ArgumentCount, _In_reads_(ArgumentCount) LPWSTR *Arguments)
{
    HANDLE Thread;

    UNREFERENCED_PARAMETER(ArgumentCount);
    UNREFERENCED_PARAMETER(Arguments);

    DiagDataState.BootMilliseconds = GetTickCount64();
    ServiceStatusHandle = RegisterServiceCtrlHandlerExW(ServiceName, ServiceControlHandler, NULL);
    if (ServiceStatusHandle == NULL)
        return;

    if (DiagDataLevel() == DIAGDATA_OFF_LEVEL)
        goto Stop;

    StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (StopEvent == NULL ||
        RegCreateKeyExW(HKEY_LOCAL_MACHINE, DIAGDATA_KEY, 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &DiagDataKey, NULL) != ERROR_SUCCESS)
    {
        DiagDataKey = NULL;
        goto Stop;
    }

    LoadInstallId();
    LoadProgress();
    OpenSession();
    ReadText(DiagDataKey, NULL, L"Channel", DiagDataState.Channel, ARRAYSIZE(DiagDataState.Channel));
    if (!ReadText(DiagDataKey, NULL, L"Endpoint", DiagDataState.Endpoint, ARRAYSIZE(DiagDataState.Endpoint)))
        lstrcpynW(DiagDataState.Endpoint, DIAGDATA_ENDPOINT, ARRAYSIZE(DiagDataState.Endpoint));

    DPRINT("DIAGDATA: previous shutdown %s\n", DiagDataState.PreviousShutdown);
    UpdateServiceStatus(SERVICE_RUNNING);
    Thread = CreateThread(NULL, 0, ReportThread, NULL, 0, NULL);
    if (Thread != NULL)
        CloseHandle(Thread);
    WaitForSingleObject(StopEvent, INFINITE);

Stop:
    if (DiagDataKey != NULL)
        RegCloseKey(DiagDataKey);
    DiagDataKey = NULL;
    UpdateServiceStatus(SERVICE_STOPPED);
}

int wmain(_In_ int ArgumentCount, _In_reads_(ArgumentCount) WCHAR *Arguments[])
{
    SERVICE_TABLE_ENTRYW ServiceTable[] = {{(LPWSTR)ServiceName, ServiceMain}, {NULL, NULL}};

    UNREFERENCED_PARAMETER(ArgumentCount);
    UNREFERENCED_PARAMETER(Arguments);
    return StartServiceCtrlDispatcherW(ServiceTable) ? 0 : (int)GetLastError();
}
