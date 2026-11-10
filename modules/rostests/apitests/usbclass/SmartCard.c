/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that every smart card reader is served through the smart card API
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>
#include <setupapi.h>
#include <winscard.h>
#include <initguid.h>

DEFINE_GUID(TEST_GUID_DEVINTERFACE_SMARTCARD_READER, 0x50DD5230, 0xBA8A, 0x11D1, 0xBF, 0x5D, 0x00, 0x00, 0xF8, 0x05, 0xF5, 0x30);

static
ULONG
CountReaderInterfaces(VOID)
{
    SP_DEVICE_INTERFACE_DATA Interface;
    HDEVINFO Set;
    ULONG Count = 0;

    Set = SetupDiGetClassDevsW(&TEST_GUID_DEVINTERFACE_SMARTCARD_READER, NULL, NULL,
                               DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Set == INVALID_HANDLE_VALUE)
        return 0;
    Interface.cbSize = sizeof(Interface);
    while (SetupDiEnumDeviceInterfaces(Set, NULL, &TEST_GUID_DEVINTERFACE_SMARTCARD_READER, Count, &Interface))
        ++Count;
    SetupDiDestroyDeviceInfoList(Set);
    return Count;
}

typedef struct _WAITER
{
    SCARDCONTEXT Context;
    LPCWSTR Reader;
    DWORD State;
    LONG Result;
} WAITER;

static
DWORD
WINAPI
WaitForChange(PVOID Parameter)
{
    WAITER *Waiter = Parameter;
    SCARD_READERSTATEW State = { 0 };

    State.szReader = Waiter->Reader;
    State.dwCurrentState = Waiter->State;
    Waiter->Result = SCardGetStatusChangeW(Waiter->Context, 10000, &State, 1);
    return 0;
}

static
VOID
TraceAttribute(
    SCARDHANDLE Card,
    DWORD Id,
    LPCSTR Name)
{
    BYTE Value[64];
    CHAR Text[3 * sizeof(Value) + 1];
    DWORD Length = sizeof(Value), Index;
    LONG Result;

    Result = SCardGetAttrib(Card, Id, Value, &Length);
    Text[0] = 0;
    for (Index = 0; Result == SCARD_S_SUCCESS && Index < Length && Index < sizeof(Value); ++Index)
        sprintf(Text + 3 * Index, "%02x ", Value[Index]);
    trace("%s: %08lx length %lu %s\n", Name, Result, Length, Text);
}

static
VOID
CheckUlong(
    SCARDHANDLE Card,
    DWORD Id,
    DWORD Expected)
{
    DWORD Value = 0xdeadbeef, Length = sizeof(Value);

    ok_hex(SCardGetAttrib(Card, Id, (LPBYTE)&Value, &Length), SCARD_S_SUCCESS);
    ok_int(Length, sizeof(Value));
    ok(Value == Expected, "Attribute %08lx is %lu, expected %lu\n", Id, Value, Expected);
}

static
VOID
TestReader(
    SCARDCONTEXT Context,
    LPCWSTR Reader)
{
    SCARD_READERSTATEW State = { 0 };
    SCARDHANDLE Card;
    DWORD Protocol, Length, Channel, Protocols, Present;
    BYTE Attribute[256];
    CHAR Vendor[128], IfdType[128], Expected[300];
    WCHAR Friendly[256], Name[256];
    BYTE Atr[36];
    DWORD NameLength, AtrLength, CardState, CardProtocol;
    SCARDHANDLE Second;
    BOOLEAN Qemu;
    WAITER Waiter;
    HANDLE Thread;
    LONG Result;
    ULONG Tries;

    State.szReader = Reader;
    State.dwCurrentState = SCARD_STATE_UNAWARE;
    ok_hex(SCardGetStatusChangeW(Context, 0, &State, 1), SCARD_S_SUCCESS);
    trace("Reader '%S' state %08lx\n", Reader, State.dwEventState);
    ok(State.dwEventState & SCARD_STATE_CHANGED, "Reader '%S' state %08lx is not marked changed\n", Reader,
       State.dwEventState);
    Present = State.dwEventState & SCARD_STATE_PRESENT;
    ok(!Present != !(State.dwEventState & SCARD_STATE_EMPTY), "Reader '%S' state %08lx is neither empty nor present\n",
       Reader, State.dwEventState);

    State.dwCurrentState = State.dwEventState & ~SCARD_STATE_CHANGED;
    ok_hex(SCardGetStatusChangeW(Context, 20, &State, 1), SCARD_E_TIMEOUT);

    Waiter.Context = Context;
    Waiter.Reader = Reader;
    Waiter.State = State.dwCurrentState;
    Waiter.Result = -1;
    Thread = CreateThread(NULL, 0, WaitForChange, &Waiter, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread)
    {
        for (Tries = 0; Tries < 500 && WaitForSingleObject(Thread, 10) == WAIT_TIMEOUT; ++Tries)
            SCardCancel(Context);
        ok(Tries < 500, "Cancelling the wait on '%S' did not end it\n", Reader);
        WaitForSingleObject(Thread, INFINITE);
        CloseHandle(Thread);
        ok_hex(Waiter.Result, SCARD_E_CANCELLED);
    }

    Card = 0;
    Protocol = 0xdeadbeef;
    ok_hex(SCardConnectW(Context, Reader, SCARD_SHARE_DIRECT, 0, &Card, &Protocol), SCARD_S_SUCCESS);
    ok_hex(Protocol, 0);
    if (!Card)
        return;

    Length = sizeof(Vendor) - 1;
    ok_hex(SCardGetAttrib(Card, SCARD_ATTR_VENDOR_NAME, (LPBYTE)Vendor, &Length), SCARD_S_SUCCESS);
    Vendor[min(Length, sizeof(Vendor) - 1)] = 0;
    ok(Length > 1, "Reader '%S' has no vendor name\n", Reader);
    Length = sizeof(IfdType) - 1;
    ok_hex(SCardGetAttrib(Card, SCARD_ATTR_VENDOR_IFD_TYPE, (LPBYTE)IfdType, &Length), SCARD_S_SUCCESS);
    IfdType[min(Length, sizeof(IfdType) - 1)] = 0;
    ok(Length > 1, "Reader '%S' has no reader type\n", Reader);
    trace("Reader '%S' vendor '%s' type '%s'\n", Reader, Vendor, IfdType);
    sprintf(Expected, "%s %s ", Vendor, IfdType);
    ok(WideCharToMultiByte(CP_ACP, 0, Reader, -1, (LPSTR)Attribute, sizeof(Attribute), NULL, NULL) &&
       !strncmp((LPSTR)Attribute, Expected, strlen(Expected)),
       "Reader '%S' is not named after '%s'\n", Reader, Expected);

    Channel = 0;
    Length = sizeof(Channel);
    ok_hex(SCardGetAttrib(Card, SCARD_ATTR_CHANNEL_ID, (LPBYTE)&Channel, &Length), SCARD_S_SUCCESS);

    Protocols = 0;
    Length = sizeof(Protocols);
    ok_hex(SCardGetAttrib(Card, SCARD_ATTR_PROTOCOL_TYPES, (LPBYTE)&Protocols, &Length), SCARD_S_SUCCESS);
    ok(Protocols && !(Protocols & ~(SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1)), "Reader '%S' protocols %08lx\n", Reader,
       Protocols);

    Length = sizeof(Friendly);
    ok_hex(SCardGetAttrib(Card, SCARD_ATTR_DEVICE_FRIENDLY_NAME_W, (LPBYTE)Friendly, &Length), SCARD_S_SUCCESS);
    ok(!wcscmp(Friendly, Reader), "Reader '%S' friendly name '%S'\n", Reader, Friendly);

    Qemu = !strcmp(Vendor, "QEMU") && !strcmp(IfdType, "QEMU USB CCID");
    ok_hex(Channel, 0x00200000);
    Length = sizeof(Attribute);
    ok_hex(SCardGetAttrib(Card, SCARD_ATTR_VENDOR_IFD_SERIAL_NO, Attribute, &Length), SCARD_S_SUCCESS);
    ok(Length > 0 && Length < sizeof(Attribute) && Attribute[Length - 1] != 0, "Serial number length %lu\n", Length);
    if (Qemu)
    {
        ok_hex(Protocols, SCARD_PROTOCOL_T0);
        ok(Length >= 2 && !memcmp(Attribute, "1-", 2), "QEMU serial number does not start with its serial\n");
        CheckUlong(Card, SCARD_ATTR_DEVICE_UNIT, 0);
        CheckUlong(Card, SCARD_ATTR_VENDOR_IFD_VERSION, 0);
        CheckUlong(Card, SCARD_ATTR_CHARACTERISTICS, 0);
        CheckUlong(Card, SCARD_ATTR_DEFAULT_CLK, 4000);
        CheckUlong(Card, SCARD_ATTR_MAX_CLK, 65536);
        CheckUlong(Card, SCARD_ATTR_DEFAULT_DATA_RATE, 9600);
        CheckUlong(Card, SCARD_ATTR_MAX_DATA_RATE, 115200);
        CheckUlong(Card, SCARD_ATTR_MAX_IFSD, 254);
        CheckUlong(Card, SCARD_ATTR_POWER_MGMT_SUPPORT, 0);
    }
    Length = sizeof(Attribute);
    ok_hex(SCardGetAttrib(Card, SCARD_ATTR_ICC_PRESENCE, Attribute, &Length), SCARD_S_SUCCESS);
    ok_int(Length, 1);
    ok(!Attribute[0] == !Present, "Card presence %u\n", Attribute[0]);
    Length = sizeof(Attribute);
    ok_hex(SCardGetAttrib(Card, SCARD_ATTR_ICC_INTERFACE_STATUS, Attribute, &Length), SCARD_S_SUCCESS);
    ok_int(Length, 1);
    if (!Present)
    {
        ok_int(Attribute[0], 0);
        Length = sizeof(Attribute);
        ok_hex(SCardGetAttrib(Card, SCARD_ATTR_ATR_STRING, Attribute, &Length), ERROR_BAD_COMMAND);
        Length = sizeof(Attribute);
        ok_hex(SCardGetAttrib(Card, SCARD_ATTR_CURRENT_PROTOCOL_TYPE, Attribute, &Length), ERROR_BAD_COMMAND);
    }
    Length = sizeof(Attribute);
    ok_hex(SCardGetAttrib(Card, SCARD_ATTR_DEVICE_SYSTEM_NAME_A, Attribute, &Length), SCARD_S_SUCCESS);
    WideCharToMultiByte(CP_ACP, 0, Reader, -1, Expected, sizeof(Expected), NULL, NULL);
    ok(Length == strlen(Expected) + 1 && !strcmp((LPSTR)Attribute, Expected), "System name '%.*s'\n", (int)Length,
       Attribute);
    TraceAttribute(Card, SCARD_ATTR_DEVICE_IN_USE, "DEVICE_IN_USE");

    NameLength = RTL_NUMBER_OF(Name);
    AtrLength = sizeof(Atr);
    CardState = CardProtocol = 0xdeadbeef;
    ok_hex(SCardStatusW(Card, Name, &NameLength, &CardState, &CardProtocol, Atr, &AtrLength), SCARD_S_SUCCESS);
    ok(!wcscmp(Name, Reader), "Status names '%S'\n", Name);
    if (!Present)
    {
        ok_hex(CardState, SCARD_ABSENT);
        ok_hex(CardProtocol, 0);
        ok_int(AtrLength, 0);
    }
    ok_hex(SCardBeginTransaction(Card), SCARD_S_SUCCESS);
    ok_hex(SCardEndTransaction(Card, SCARD_LEAVE_CARD), SCARD_S_SUCCESS);
    Second = 0;
    ok_hex(SCardConnectW(Context, Reader, SCARD_SHARE_DIRECT, 0, &Second, &Protocol), SCARD_E_SHARING_VIOLATION);
    if (Second)
        SCardDisconnect(Second, SCARD_LEAVE_CARD);
    Second = 0;
    ok_hex(SCardConnectW(Context, Reader, SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &Second,
                         &Protocol), SCARD_E_SHARING_VIOLATION);
    if (Second)
        SCardDisconnect(Second, SCARD_LEAVE_CARD);

    ok_hex(SCardDisconnect(Card, SCARD_LEAVE_CARD), SCARD_S_SUCCESS);

    if (!Present)
    {
        Card = 0;
        Result = SCardConnectW(Context, Reader, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &Card,
                               &Protocol);
        ok_hex(Result, SCARD_W_REMOVED_CARD);
        if (Result == SCARD_S_SUCCESS)
            SCardDisconnect(Card, SCARD_LEAVE_CARD);
    }
}

START_TEST(SmartCard)
{
    SCARDCONTEXT Context = 0;
    LPWSTR Readers = NULL, Reader;
    DWORD Length = SCARD_AUTOALLOCATE;
    ULONG Interfaces, Listed = 0;
    LONG Result;

    Interfaces = CountReaderInterfaces();
    if (!Interfaces)
    {
        skip("No smart card reader is present\n");
        return;
    }
    trace("%lu smart card reader interfaces\n", Interfaces);

    ok_hex(SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &Context), SCARD_S_SUCCESS);
    if (!Context)
        return;
    ok_hex(SCardIsValidContext(Context), SCARD_S_SUCCESS);

    Result = SCardListReadersW(Context, NULL, (LPWSTR)&Readers, &Length);
    ok_hex(Result, SCARD_S_SUCCESS);
    if (Result == SCARD_S_SUCCESS)
    {
        for (Reader = Readers; *Reader; Reader += wcslen(Reader) + 1)
        {
            ++Listed;
            TestReader(Context, Reader);
        }
        ok_int(Length, (DWORD)(Reader - Readers + 1));
        SCardFreeMemory(Context, Readers);
    }
    ok_int(Listed, Interfaces);

    {
        WCHAR Groups[256];
        DWORD GroupsLength = RTL_NUMBER_OF(Groups);
        SCARD_READERSTATEW Pnp = { 0 };
        SCARDHANDLE Card = 0;
        DWORD Protocol;

        ok_hex(SCardListReaderGroupsW(Context, Groups, &GroupsLength), SCARD_S_SUCCESS);
        ok_int(GroupsLength, 22);
        ok(!memcmp(Groups, L"SCard$DefaultReaders\0", 22 * sizeof(WCHAR)), "Reader groups '%S'\n", Groups);
        Pnp.szReader = L"\\\\?PnP?\\Notification";
        Pnp.dwCurrentState = Listed << 16;
        ok_hex(SCardGetStatusChangeW(Context, 0, &Pnp, 1), SCARD_E_TIMEOUT);
        ok_hex(Pnp.dwEventState, 0);
        ok_hex(SCardConnectW(Context, L"No Such Reader 0", SCARD_SHARE_DIRECT, 0, &Card, &Protocol),
               SCARD_E_UNKNOWN_READER);
    }

    ok_hex(SCardReleaseContext(Context), SCARD_S_SUCCESS);
    ok_hex(SCardIsValidContext(Context), ERROR_INVALID_HANDLE);
}
