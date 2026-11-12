/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SetCalendarInfoA/W and the two-digit year user override
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <ndk/cmfuncs.h>
#include <ndk/obfuncs.h>

#define TWO_DIGIT_YEAR_MAX_KEY L"Control Panel\\International\\Calendars\\TwoDigitYearMax"

typedef struct _SAVED_VALUE
{
    BOOL Present;
    ULONG Type;
    ULONG Size;
    UCHAR Data[128];
} SAVED_VALUE;

static HANDLE OpenTwoDigitYearMaxKey(ACCESS_MASK Access)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE User, Key = NULL;

    if (!NT_SUCCESS(RtlOpenCurrentUser(KEY_READ, &User)))
        return NULL;
    RtlInitUnicodeString(&KeyName, TWO_DIGIT_YEAR_MAX_KEY);
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, User, NULL);
    if (!NT_SUCCESS(NtOpenKey(&Key, Access, &ObjectAttributes)))
        Key = NULL;
    NtClose(User);
    return Key;
}

static BOOL QueryStoredValue(PCWSTR Name, ULONG *Type, PVOID Data, ULONG *Size)
{
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 128];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING ValueName;
    ULONG ResultLength;
    NTSTATUS Status;
    HANDLE Key;

    Key = OpenTwoDigitYearMaxKey(KEY_QUERY_VALUE);
    if (!Key)
        return FALSE;
    RtlInitUnicodeString(&ValueName, Name);
    Status = NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Info, sizeof(Buffer), &ResultLength);
    NtClose(Key);
    if (!NT_SUCCESS(Status) || Info->DataLength > *Size)
        return FALSE;
    *Type = Info->Type;
    *Size = Info->DataLength;
    memcpy(Data, Info->Data, Info->DataLength);
    return TRUE;
}

static void SaveValue(PCWSTR Name, SAVED_VALUE *Saved)
{
    Saved->Size = sizeof(Saved->Data);
    Saved->Present = QueryStoredValue(Name, &Saved->Type, Saved->Data, &Saved->Size);
}

static void RestoreValue(PCWSTR Name, const SAVED_VALUE *Saved)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;
    HANDLE Key;

    Key = OpenTwoDigitYearMaxKey(KEY_SET_VALUE);
    if (!Key)
    {
        ok(!Saved->Present, "Cannot reopen the TwoDigitYearMax key to restore value %S\n", Name);
        return;
    }
    RtlInitUnicodeString(&ValueName, Name);
    if (Saved->Present)
        Status = NtSetValueKey(Key, &ValueName, 0, Saved->Type, (PVOID)Saved->Data, Saved->Size);
    else
        Status = NtDeleteValueKey(Key, &ValueName);
    ok(NT_SUCCESS(Status) || (!Saved->Present && Status == STATUS_OBJECT_NAME_NOT_FOUND),
       "Restoring value %S failed: 0x%lx\n", Name, Status);
    NtClose(Key);
}

static DWORD GetTwoDigitYearMax(LCID Locale, CALID Calendar, CALTYPE Flags)
{
    DWORD Value = 0xdeadbeef;
    int Ret;

    Ret = GetCalendarInfoW(Locale, Calendar, CAL_ITWODIGITYEARMAX | CAL_RETURN_NUMBER | Flags, NULL, 0, &Value);
    ok(Ret == sizeof(DWORD) / sizeof(WCHAR), "GetCalendarInfoW returned %d, error %lu\n", Ret, GetLastError());
    return Value;
}

static void TestUnicode(DWORD Default)
{
    int (WINAPI *pGetCalendarInfoEx)(LPCWSTR, CALID, LPCWSTR, CALTYPE, LPWSTR, int, LPDWORD);
    WCHAR BufferW[20];
    CHAR BufferA[20];
    WCHAR Stored[20];
    ULONG Type, Size;
    DWORD Value;
    int Ret;

    SetLastError(0xdeadbeef);
    Ret = SetCalendarInfoW(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX, L"2041");
    ok(Ret != 0, "SetCalendarInfoW failed, error %lu\n", GetLastError());

    ok(GetTwoDigitYearMax(LOCALE_USER_DEFAULT, CAL_GREGORIAN, 0) == 2041, "Override not returned\n");
    ok(GetTwoDigitYearMax(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_NOUSEROVERRIDE) == Default,
       "CAL_NOUSEROVERRIDE did not return the database value %lu\n", Default);

    memset(BufferW, 0x55, sizeof(BufferW));
    Ret = GetCalendarInfoW(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX, BufferW, ARRAY_SIZE(BufferW), NULL);
    ok(Ret == 5, "GetCalendarInfoW returned %d\n", Ret);
    ok(wcscmp(BufferW, L"2041") == 0, "Got %S\n", BufferW);

    memset(BufferA, 0x55, sizeof(BufferA));
    Ret = GetCalendarInfoA(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX, BufferA, sizeof(BufferA), NULL);
    ok(Ret == 5, "GetCalendarInfoA returned %d\n", Ret);
    ok(strcmp(BufferA, "2041") == 0, "Got %s\n", BufferA);

    Value = 0xdeadbeef;
    Ret = GetCalendarInfoA(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX | CAL_RETURN_NUMBER, NULL, 0, &Value);
    ok(Ret == sizeof(DWORD), "GetCalendarInfoA returned %d\n", Ret);
    ok(Value == 2041, "GetCalendarInfoA got %lu\n", Value);

    pGetCalendarInfoEx = (PVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCalendarInfoEx");
    ok(pGetCalendarInfoEx != NULL, "GetCalendarInfoEx is not exported\n");
    if (pGetCalendarInfoEx)
    {
        Value = 0xdeadbeef;
        Ret = pGetCalendarInfoEx(LOCALE_NAME_USER_DEFAULT, CAL_GREGORIAN, NULL,
                                 CAL_ITWODIGITYEARMAX | CAL_RETURN_NUMBER, NULL, 0, &Value);
        ok(Ret == sizeof(DWORD) / sizeof(WCHAR), "GetCalendarInfoEx returned %d\n", Ret);
        ok(Value == 2041, "GetCalendarInfoEx got %lu\n", Value);
    }

    ok(GetTwoDigitYearMax(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT), CAL_GREGORIAN, 0) == 2041,
       "Override is not per calendar\n");

    Type = REG_NONE;
    Size = sizeof(Stored);
    memset(Stored, 0, sizeof(Stored));
    ok(QueryStoredValue(L"1", &Type, Stored, &Size), "Override is not stored under Calendars\\TwoDigitYearMax\n");
    ok(Type == REG_SZ, "Stored type %lu\n", Type);
    ok(Size == sizeof(L"2041") && wcscmp(Stored, L"2041") == 0, "Stored %S (%lu bytes)\n", Stored, Size);

    Ret = SetCalendarInfoW(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX | CAL_USE_CP_ACP, L"2047");
    ok(Ret != 0, "SetCalendarInfoW with CAL_USE_CP_ACP failed, error %lu\n", GetLastError());
    ok(GetTwoDigitYearMax(LOCALE_USER_DEFAULT, CAL_GREGORIAN, 0) == 2047, "CAL_USE_CP_ACP value not stored\n");
}

static void TestAnsi(void)
{
    int Ret;

    SetLastError(0xdeadbeef);
    Ret = SetCalendarInfoA(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX, "2045");
    ok(Ret != 0, "SetCalendarInfoA failed, error %lu\n", GetLastError());
    ok(GetTwoDigitYearMax(LOCALE_USER_DEFAULT, CAL_GREGORIAN, 0) == 2045, "SetCalendarInfoA value not returned\n");

    Ret = SetCalendarInfoA(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX | CAL_USE_CP_ACP, "2046");
    ok(Ret != 0, "SetCalendarInfoA with CAL_USE_CP_ACP failed, error %lu\n", GetLastError());
    ok(GetTwoDigitYearMax(LOCALE_USER_DEFAULT, CAL_GREGORIAN, 0) == 2046, "SetCalendarInfoA value not returned\n");

    SetLastError(0xdeadbeef);
    Ret = SetCalendarInfoA(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX, NULL);
    ok(Ret == 0, "SetCalendarInfoA(NULL) returned %d\n", Ret);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = SetCalendarInfoA(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_SSHORTDATE, "yyyy");
    ok(Ret == 0, "SetCalendarInfoA(CAL_SSHORTDATE) returned %d\n", Ret);
    ok(GetLastError() == ERROR_INVALID_FLAGS, "Got error %lu\n", GetLastError());
}

static void TestOtherCalendar(void)
{
    LCID English = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);
    WCHAR Buffer[20];
    int Ret;

    SetLastError(0xdeadbeef);
    Ret = SetCalendarInfoW(English, CAL_JAPAN, CAL_ITWODIGITYEARMAX, L"98");
    ok(Ret != 0, "SetCalendarInfoW(CAL_JAPAN) failed, error %lu\n", GetLastError());

    memset(Buffer, 0x55, sizeof(Buffer));
    Ret = GetCalendarInfoW(English, CAL_JAPAN, CAL_ITWODIGITYEARMAX, Buffer, ARRAY_SIZE(Buffer), NULL);
    ok(Ret == 3, "GetCalendarInfoW returned %d\n", Ret);
    ok(wcscmp(Buffer, L"98") == 0, "Got %S\n", Buffer);
    ok(GetTwoDigitYearMax(LOCALE_USER_DEFAULT, CAL_GREGORIAN, 0) == 2046, "CAL_JAPAN override changed CAL_GREGORIAN\n");
}

static void TestInvalid(void)
{
    static const struct
    {
        LCID Locale;
        CALID Calendar;
        CALTYPE Type;
        PCWSTR Data;
        DWORD Error;
    } Tests[] =
    {
        { LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_SSHORTDATE, L"yyyy", ERROR_INVALID_FLAGS },
        { LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX | CAL_RETURN_NUMBER, L"2041", ERROR_INVALID_FLAGS },
        { LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX, NULL, ERROR_INVALID_PARAMETER },
        { LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX, L"", ERROR_INVALID_PARAMETER },
        { LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX, L"20x1", ERROR_INVALID_PARAMETER },
        { LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX, L"99999999999", ERROR_INVALID_PARAMETER },
        { LOCALE_USER_DEFAULT, 0, CAL_ITWODIGITYEARMAX, L"2041", ERROR_INVALID_PARAMETER },
        { 0xdeadbeef, CAL_GREGORIAN, CAL_ITWODIGITYEARMAX, L"2041", ERROR_INVALID_PARAMETER },
    };
    ULONG i;
    int Ret;

    for (i = 0; i < ARRAY_SIZE(Tests); i++)
    {
        SetLastError(0xdeadbeef);
        Ret = SetCalendarInfoW(Tests[i].Locale, Tests[i].Calendar, Tests[i].Type, Tests[i].Data);
        ok(Ret == 0, "Test %lu: SetCalendarInfoW returned %d\n", i, Ret);
        ok(GetLastError() == Tests[i].Error, "Test %lu: got error %lu, expected %lu\n", i, GetLastError(), Tests[i].Error);
    }

    ok(GetTwoDigitYearMax(LOCALE_USER_DEFAULT, CAL_GREGORIAN, 0) == 2046, "A failed call changed the override\n");
}

START_TEST(SetCalendarInfo)
{
    SAVED_VALUE SavedGregorian, SavedJapan;
    DWORD Default;

    SaveValue(L"1", &SavedGregorian);
    SaveValue(L"3", &SavedJapan);

    Default = GetTwoDigitYearMax(LOCALE_USER_DEFAULT, CAL_GREGORIAN, CAL_NOUSEROVERRIDE);
    ok(Default >= 2000 && Default < 2100, "Database value %lu\n", Default);

    TestUnicode(Default);
    TestAnsi();
    TestOtherCalendar();
    TestInvalid();

    RestoreValue(L"1", &SavedGregorian);
    RestoreValue(L"3", &SavedJapan);
}
