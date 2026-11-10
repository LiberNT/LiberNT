/*
 * PROJECT:     LiberNT Smart Card Service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Service entry, reader arrival and removal, card tracking
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "scardsvr.h"

#include <initguid.h>

#define NDEBUG
#include <debug.h>

DEFINE_GUID(ScReaderInterface, 0x50DD5230, 0xBA8A, 0x11D1, 0xBF, 0x5D, 0x00, 0x00, 0xF8, 0x05, 0xF5, 0x30);

CRITICAL_SECTION ScLock;
CONDITION_VARIABLE ScChanged;
LIST_ENTRY ScReaders;

static SERVICE_STATUS_HANDLE ScStatusHandle;
static SERVICE_STATUS ScServiceStatus;
static HANDLE ScStopEvent;
static HDEVNOTIFY ScNotify;

LONG
ScMapError(
    _In_ DWORD Error)
{
    switch (Error)
    {
        case ERROR_SUCCESS:
            return SCARD_S_SUCCESS;

        case ERROR_INSUFFICIENT_BUFFER:
        case ERROR_MORE_DATA:
            return SCARD_E_INSUFFICIENT_BUFFER;

        case ERROR_NOT_READY:
        case ERROR_UNRECOGNIZED_MEDIA:
            return SCARD_W_REMOVED_CARD;

        case ERROR_SEM_TIMEOUT:
        case ERROR_TIMEOUT:
            return SCARD_W_UNRESPONSIVE_CARD;

        case ERROR_DEVICE_REMOVED:
        case ERROR_DEV_NOT_EXIST:
        case ERROR_OPERATION_ABORTED:
            return SCARD_E_READER_UNAVAILABLE;

        default:
            return (LONG)Error;
    }
}

DWORD
ScIoctl(
    _In_ SC_READER *Reader,
    _In_ DWORD Code,
    _In_reads_bytes_opt_(InLength) PVOID In,
    _In_ DWORD InLength,
    _Out_writes_bytes_opt_(OutLength) PVOID Out,
    _In_ DWORD OutLength,
    _Out_ PDWORD Returned)
{
    OVERLAPPED Overlapped = { 0 };
    DWORD Error = ERROR_SUCCESS;

    *Returned = 0;
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Overlapped.hEvent)
        return GetLastError();
    if (!DeviceIoControl(Reader->Device, Code, In, InLength, Out, OutLength, Returned, &Overlapped))
    {
        Error = GetLastError();
        if (Error == ERROR_IO_PENDING)
            Error = GetOverlappedResult(Reader->Device, &Overlapped, Returned, TRUE) ? ERROR_SUCCESS : GetLastError();
    }
    CloseHandle(Overlapped.hEvent);
    return Error;
}

static
VOID
ScUpdateCard(
    _Inout_ SC_READER *Reader,
    _In_ DWORD State,
    _In_reads_bytes_(AtrLength) const BYTE *Atr,
    _In_ DWORD AtrLength,
    _In_ BOOL Mute)
{
    EnterCriticalSection(&ScLock);
    Reader->State = State;
    Reader->Mute = Mute;
    Reader->AtrLength = min(AtrLength, sizeof(Reader->Atr));
    if (Reader->AtrLength)
        memcpy(Reader->Atr, Atr, Reader->AtrLength);
    if (State < SCARD_SPECIFIC)
        Reader->Protocol = SCARD_PROTOCOL_UNDEFINED;
    WakeAllConditionVariable(&ScChanged);
    LeaveCriticalSection(&ScLock);
}

LONG
ScPowerCard(
    _Inout_ SC_READER *Reader,
    _In_ DWORD Disposition)
{
    BYTE Atr[SCARDSVR_MAX_ATR];
    DWORD Minor, Returned = 0, State = SCARD_UNKNOWN, Error;

    switch (Disposition)
    {
        case SCARD_RESET_CARD:
            Minor = SCARD_COLD_RESET;
            break;

        case SCARD_UNPOWER_CARD:
            Minor = SCARD_POWER_DOWN;
            break;

        case SCARD_EJECT_CARD:
            Error = ScIoctl(Reader, IOCTL_SMARTCARD_EJECT, NULL, 0, NULL, 0, &Returned);
            if (Error != ERROR_SUCCESS && Error != ERROR_NOT_SUPPORTED && Error != ERROR_INVALID_FUNCTION)
                return ScMapError(Error);
            Minor = SCARD_POWER_DOWN;
            break;

        default:
            return SCARD_S_SUCCESS;
    }
    Error = ScIoctl(Reader, IOCTL_SMARTCARD_POWER, &Minor, sizeof(Minor), Atr, sizeof(Atr), &Returned);
    if (Minor != SCARD_POWER_DOWN && Error != ERROR_SUCCESS)
    {
        DPRINT1("SCARDSVR: powering the card in '%s' failed %lu\n", Reader->Name, Error);
        ScUpdateCard(Reader, SCARD_SWALLOWED, NULL, 0, TRUE);
        return Error == ERROR_NOT_READY ? SCARD_W_REMOVED_CARD : SCARD_W_UNRESPONSIVE_CARD;
    }
    if (ScIoctl(Reader, IOCTL_SMARTCARD_GET_STATE, NULL, 0, &State, sizeof(State), &Returned) != ERROR_SUCCESS)
        State = SCARD_UNKNOWN;
    ScUpdateCard(Reader, State, Atr, Minor == SCARD_POWER_DOWN ? 0 : Returned, FALSE);
    EnterCriticalSection(&ScLock);
    ++Reader->Resets;
    LeaveCriticalSection(&ScLock);
    return ScMapError(Error);
}

LONG
ScSetProtocol(
    _Inout_ SC_READER *Reader,
    _In_ DWORD Preferred,
    _Out_ PDWORD Protocol)
{
    DWORD Returned = 0, Selected = SCARD_PROTOCOL_UNDEFINED, Error;
    LONG Result;

    *Protocol = SCARD_PROTOCOL_UNDEFINED;
    if (Reader->State < SCARD_NEGOTIABLE)
    {
        Result = ScPowerCard(Reader, SCARD_RESET_CARD);
        if (Result != SCARD_S_SUCCESS)
            return Result;
    }
    if (Reader->State == SCARD_SPECIFIC && Reader->Protocol)
    {
        if (!(Reader->Protocol & Preferred))
            return SCARD_E_PROTO_MISMATCH;
        *Protocol = Reader->Protocol;
        return SCARD_S_SUCCESS;
    }
    Error = ScIoctl(Reader, IOCTL_SMARTCARD_SET_PROTOCOL, &Preferred, sizeof(Preferred), &Selected, sizeof(Selected),
                    &Returned);
    if (Error == ERROR_NOT_SUPPORTED)
        return SCARD_E_PROTO_MISMATCH;
    if (Error != ERROR_SUCCESS)
        return ScMapError(Error);
    EnterCriticalSection(&ScLock);
    Reader->State = SCARD_SPECIFIC;
    Reader->Protocol = Selected;
    WakeAllConditionVariable(&ScChanged);
    LeaveCriticalSection(&ScLock);
    *Protocol = Selected;
    return SCARD_S_SUCCESS;
}

VOID
ScReferenceReader(
    _Inout_ SC_READER *Reader)
{
    InterlockedIncrement(&Reader->References);
}

VOID
ScDereferenceReader(
    _Inout_ SC_READER *Reader)
{
    if (InterlockedDecrement(&Reader->References))
        return;
    ASSERT(!Reader->Listed);
    CloseHandle(Reader->Device);
    DeleteCriticalSection(&Reader->IoLock);
    HeapFree(GetProcessHeap(), 0, Reader);
}

static
VOID
ScUnlistReader(
    _Inout_ SC_READER *Reader)
{
    BOOL Unlisted = FALSE;

    EnterCriticalSection(&ScLock);
    Reader->Removed = TRUE;
    if (Reader->Listed)
    {
        RemoveEntryList(&Reader->Entry);
        Reader->Listed = FALSE;
        Unlisted = TRUE;
    }
    WakeAllConditionVariable(&ScChanged);
    LeaveCriticalSection(&ScLock);
    if (Unlisted)
    {
        DPRINT1("SCARDSVR: reader '%s' removed\n", Reader->Name);
        ScDereferenceReader(Reader);
    }
}

static
DWORD
WINAPI
ScTrackReader(
    _In_ PVOID Parameter)
{
    SC_READER *Reader = Parameter;
    DWORD State, Returned, Code, Error;
    BOOL Present, WasPresent = FALSE, First = TRUE;

    for (;;)
    {
        State = SCARD_UNKNOWN;
        Error = ScIoctl(Reader, IOCTL_SMARTCARD_GET_STATE, NULL, 0, &State, sizeof(State), &Returned);
        if (Error != ERROR_SUCCESS)
            break;
        Present = State > SCARD_ABSENT;
        if (First || Present != WasPresent)
        {
            EnterCriticalSection(&ScLock);
            if (!First)
                ++Reader->Events;
            Reader->Mute = FALSE;
            LeaveCriticalSection(&ScLock);
            ScUpdateCard(Reader, State, NULL, 0, FALSE);
            if (Present && State < SCARD_NEGOTIABLE)
            {
                EnterCriticalSection(&Reader->IoLock);
                ScPowerCard(Reader, SCARD_RESET_CARD);
                LeaveCriticalSection(&Reader->IoLock);
            }
            DPRINT1("SCARDSVR: '%s' card %s, state %lu\n", Reader->Name, Present ? "present" : "absent",
                    Reader->State);
        }
        First = FALSE;
        WasPresent = Present;
        Code = Present ? IOCTL_SMARTCARD_IS_ABSENT : IOCTL_SMARTCARD_IS_PRESENT;
        Error = ScIoctl(Reader, Code, NULL, 0, NULL, 0, &Returned);
        if (Error != ERROR_SUCCESS)
            break;
    }
    DPRINT1("SCARDSVR: tracking '%s' ended: %lu\n", Reader->Name, Error);
    ScUnlistReader(Reader);
    ScDereferenceReader(Reader);
    return 0;
}

static
BOOL
ScReadAttribute(
    _In_ SC_READER *Reader,
    _In_ DWORD Id,
    _Out_writes_(Size) PCHAR Buffer,
    _In_ DWORD Size)
{
    DWORD Returned = 0;

    if (ScIoctl(Reader, IOCTL_SMARTCARD_GET_ATTRIBUTE, &Id, sizeof(Id), Buffer, Size - 1, &Returned) != ERROR_SUCCESS)
        return FALSE;
    while (Returned && !Buffer[Returned - 1])
        --Returned;
    Buffer[Returned] = 0;
    return Returned != 0;
}

static
VOID
ScNameReader(
    _Inout_ SC_READER *Reader)
{
    CHAR Vendor[64], Type[64], Base[SC_MAX_NAME - 8], Name[SC_MAX_NAME];
    PLIST_ENTRY Entry;
    ULONG Unit;
    BOOL Used;

    if (!ScReadAttribute(Reader, SCARD_ATTR_VENDOR_NAME, Vendor, sizeof(Vendor)))
        StringCbCopyA(Vendor, sizeof(Vendor), "Generic");
    if (!ScReadAttribute(Reader, SCARD_ATTR_VENDOR_IFD_TYPE, Type, sizeof(Type)))
        StringCbCopyA(Type, sizeof(Type), "Smart Card Reader");
    StringCbPrintfA(Base, sizeof(Base), "%s %s", Vendor, Type);
    for (Unit = 0;; ++Unit)
    {
        StringCbPrintfA(Name, sizeof(Name), "%s %lu", Base, Unit);
        Used = FALSE;
        for (Entry = ScReaders.Flink; Entry != &ScReaders; Entry = Entry->Flink)
        {
            if (!strcmp(CONTAINING_RECORD(Entry, SC_READER, Entry)->Name, Name))
            {
                Used = TRUE;
                break;
            }
        }
        if (!Used)
            break;
    }
    StringCbCopyA(Reader->Name, sizeof(Reader->Name), Name);
}

VOID
ScAddReader(
    _In_ PCWSTR Path)
{
    SC_READER *Reader;
    PLIST_ENTRY Entry;
    HANDLE Thread;

    EnterCriticalSection(&ScLock);
    for (Entry = ScReaders.Flink; Entry != &ScReaders; Entry = Entry->Flink)
    {
        if (!_wcsicmp(CONTAINING_RECORD(Entry, SC_READER, Entry)->Path, Path))
        {
            LeaveCriticalSection(&ScLock);
            return;
        }
    }
    LeaveCriticalSection(&ScLock);

    Reader = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Reader));
    if (!Reader)
        return;
    StringCbCopyW(Reader->Path, sizeof(Reader->Path), Path);
    Reader->Device = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                                 NULL);
    if (Reader->Device == INVALID_HANDLE_VALUE)
    {
        DPRINT1("SCARDSVR: opening reader %S failed %lu\n", Path, GetLastError());
        HeapFree(GetProcessHeap(), 0, Reader);
        return;
    }
    InitializeCriticalSection(&Reader->IoLock);
    Reader->References = 2;

    EnterCriticalSection(&ScLock);
    ScNameReader(Reader);
    InsertTailList(&ScReaders, &Reader->Entry);
    Reader->Listed = TRUE;
    WakeAllConditionVariable(&ScChanged);
    LeaveCriticalSection(&ScLock);
    DPRINT1("SCARDSVR: reader '%s' at %S\n", Reader->Name, Path);

    Thread = CreateThread(NULL, 0, ScTrackReader, Reader, 0, NULL);
    if (Thread)
    {
        CloseHandle(Thread);
        return;
    }
    ScUnlistReader(Reader);
    ScDereferenceReader(Reader);
}

VOID
ScRemoveReader(
    _In_ PCWSTR Path)
{
    SC_READER *Reader = NULL;
    PLIST_ENTRY Entry;

    EnterCriticalSection(&ScLock);
    for (Entry = ScReaders.Flink; Entry != &ScReaders; Entry = Entry->Flink)
    {
        if (!_wcsicmp(CONTAINING_RECORD(Entry, SC_READER, Entry)->Path, Path))
        {
            Reader = CONTAINING_RECORD(Entry, SC_READER, Entry);
            ScReferenceReader(Reader);
            break;
        }
    }
    LeaveCriticalSection(&ScLock);
    if (!Reader)
        return;
    CancelIoEx(Reader->Device, NULL);
    ScUnlistReader(Reader);
    ScDereferenceReader(Reader);
}

static
VOID
ScEnumerateReaders(VOID)
{
    SP_DEVICE_INTERFACE_DATA Interface;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    DWORD Index, Size;
    HDEVINFO Set;

    Set = SetupDiGetClassDevsW(&ScReaderInterface, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Set == INVALID_HANDLE_VALUE)
        return;
    Size = FIELD_OFFSET(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath) + MAX_PATH * sizeof(WCHAR);
    Detail = HeapAlloc(GetProcessHeap(), 0, Size);
    Interface.cbSize = sizeof(Interface);
    for (Index = 0; Detail && SetupDiEnumDeviceInterfaces(Set, NULL, &ScReaderInterface, Index, &Interface); ++Index)
    {
        Detail->cbSize = sizeof(*Detail);
        if (SetupDiGetDeviceInterfaceDetailW(Set, &Interface, Detail, Size, NULL, NULL))
            ScAddReader(Detail->DevicePath);
    }
    if (Detail)
        HeapFree(GetProcessHeap(), 0, Detail);
    SetupDiDestroyDeviceInfoList(Set);
}

static
VOID
ScReportStatus(
    _In_ DWORD State)
{
    ScServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ScServiceStatus.dwCurrentState = State;
    ScServiceStatus.dwControlsAccepted = State == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    ScServiceStatus.dwWin32ExitCode = NO_ERROR;
    ScServiceStatus.dwWaitHint = State == SERVICE_RUNNING || State == SERVICE_STOPPED ? 0 : 5000;
    SetServiceStatus(ScStatusHandle, &ScServiceStatus);
}

static
DWORD
WINAPI
ScControlHandler(
    _In_ DWORD Control,
    _In_ DWORD EventType,
    _In_ LPVOID EventData,
    _In_ LPVOID Context)
{
    PDEV_BROADCAST_DEVICEINTERFACE_W Broadcast = EventData;

    UNREFERENCED_PARAMETER(Context);
    switch (Control)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            ScReportStatus(SERVICE_STOP_PENDING);
            SetEvent(ScStopEvent);
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus(ScStatusHandle, &ScServiceStatus);
            return NO_ERROR;

        case SERVICE_CONTROL_DEVICEEVENT:
            if (!Broadcast || Broadcast->dbcc_devicetype != DBT_DEVTYP_DEVICEINTERFACE ||
                !IsEqualGUID(&Broadcast->dbcc_classguid, &ScReaderInterface))
            {
                return NO_ERROR;
            }
            if (EventType == DBT_DEVICEARRIVAL)
                ScAddReader(Broadcast->dbcc_name);
            else if (EventType == DBT_DEVICEREMOVECOMPLETE || EventType == DBT_DEVICEQUERYREMOVE)
                ScRemoveReader(Broadcast->dbcc_name);
            return NO_ERROR;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static
VOID
WINAPI
ScServiceMain(
    _In_ DWORD Argc,
    _In_ LPWSTR *Argv)
{
    DEV_BROADCAST_DEVICEINTERFACE_W Filter = { 0 };
    DWORD Error;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);
    ScStatusHandle = RegisterServiceCtrlHandlerExW(L"SCardSvr", ScControlHandler, NULL);
    if (!ScStatusHandle)
        return;
    ScReportStatus(SERVICE_START_PENDING);
    ScStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    Filter.dbcc_size = sizeof(Filter);
    Filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    Filter.dbcc_classguid = ScReaderInterface;
    ScNotify = RegisterDeviceNotificationW(ScStatusHandle, &Filter, DEVICE_NOTIFY_SERVICE_HANDLE);
    if (!ScNotify)
        DPRINT1("SCARDSVR: reader notifications unavailable: %lu\n", GetLastError());
    ScEnumerateReaders();

    Error = ScStartRpcServer();
    if (Error != RPC_S_OK || !ScStopEvent)
    {
        DPRINT1("SCARDSVR: starting the RPC server failed %lu\n", Error);
        ScServiceStatus.dwWin32ExitCode = Error ? Error : GetLastError();
        ScServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(ScStatusHandle, &ScServiceStatus);
        return;
    }
    ScReportStatus(SERVICE_RUNNING);
    WaitForSingleObject(ScStopEvent, INFINITE);
    RpcMgmtStopServerListening(NULL);
    if (ScNotify)
        UnregisterDeviceNotification(ScNotify);
    ScReportStatus(SERVICE_STOPPED);
}

int
wmain(
    int Argc,
    WCHAR **Argv)
{
    SERVICE_TABLE_ENTRYW Table[] =
    {
        { L"SCardSvr", ScServiceMain },
        { NULL, NULL }
    };

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);
    InitializeCriticalSection(&ScLock);
    InitializeConditionVariable(&ScChanged);
    InitializeListHead(&ScReaders);
    return StartServiceCtrlDispatcherW(Table) ? 0 : (int)GetLastError();
}
