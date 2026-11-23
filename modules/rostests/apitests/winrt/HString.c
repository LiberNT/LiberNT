/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the HSTRING layout that C++/WinRT relies on
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "winrt_apitest.h"

typedef struct _SHARED_HSTRING_HEADER
{
    UINT32 Flags;
    UINT32 Length;
    UINT32 Padding1;
    UINT32 Padding2;
    const WCHAR *Ptr;
    LONG Count;
    WCHAR Buffer[1];
} SHARED_HSTRING_HEADER;

#define MaxLength ((UINT32)((MAXUINT32 - 3 - FIELD_OFFSET(SHARED_HSTRING_HEADER, Buffer)) / sizeof(WCHAR) - 1))

static void
TestSizeLimit(
    _In_ UINT32 Length)
{
    static const WCHAR Text[] = L"winget";
    HSTRING_BUFFER Buffer = (HSTRING_BUFFER)0xdeadbeef;
    WCHAR *Chars = (WCHAR *)0xdeadbeef;
    HSTRING String = (HSTRING)0xdeadbeef;
    HRESULT hr;

    hr = WindowsPreallocateStringBuffer(Length, &Chars, &Buffer);
    ok(hr == MEM_E_INVALID_SIZE, "WindowsPreallocateStringBuffer(%#x) returned 0x%08lx\n", Length, hr);
    ok(!Chars && !Buffer, "WindowsPreallocateStringBuffer(%#x): chars %p, buffer %p\n", Length, Chars, Buffer);
    if (SUCCEEDED(hr))
        WindowsDeleteStringBuffer(Buffer);

    hr = WindowsCreateString(Text, Length, &String);
    ok(hr == MEM_E_INVALID_SIZE, "WindowsCreateString(%#x) returned 0x%08lx\n", Length, hr);
    ok(String == NULL, "WindowsCreateString(%#x): string %p\n", Length, String);
    if (SUCCEEDED(hr))
        WindowsDeleteString(String);
}

START_TEST(HString)
{
    static const WCHAR Text[] = L"winget";
    SHARED_HSTRING_HEADER *Shared;
    HSTRING String = NULL, Copy = NULL;
    HSTRING_HEADER Header;
    HSTRING_BUFFER Buffer;
    WCHAR *Chars, *Large;
    HRESULT hr;

    hr = WindowsCreateString(Text, ARRAYSIZE(Text) - 1, &String);
    ok(hr == S_OK, "WindowsCreateString returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    Shared = (SHARED_HSTRING_HEADER *)String;
    ok(Shared->Flags == 0, "Flags %u\n", Shared->Flags);
    ok(Shared->Length == ARRAYSIZE(Text) - 1, "Length %u\n", Shared->Length);
    ok(Shared->Ptr == Shared->Buffer, "Ptr %p, buffer %p\n", Shared->Ptr, Shared->Buffer);
    ok(Shared->Count == 1, "Count %ld\n", Shared->Count);
    ok(!wcscmp(Shared->Buffer, Text), "Buffer %s\n", wine_dbgstr_w(Shared->Buffer));
    ok(HeapValidate(GetProcessHeap(), 0, Shared), "the string is not a process heap block\n");

    hr = WindowsDuplicateString(String, &Copy);
    ok(hr == S_OK, "WindowsDuplicateString returned 0x%08lx\n", hr);
    ok(Copy == String, "Copy %p, string %p\n", Copy, String);
    ok(Shared->Count == 2, "Count %ld\n", Shared->Count);
    hr = WindowsDeleteString(Copy);
    ok(hr == S_OK, "WindowsDeleteString returned 0x%08lx\n", hr);
    ok(Shared->Count == 1, "Count %ld\n", Shared->Count);

    /* C++/WinRT releases the last reference itself with HeapFree on the process heap */
    Shared->Count--;
    ok(HeapFree(GetProcessHeap(), 0, Shared), "HeapFree failed: %lu\n", GetLastError());

    Shared = HeapAlloc(GetProcessHeap(), 0, FIELD_OFFSET(SHARED_HSTRING_HEADER, Buffer[ARRAYSIZE(Text)]));
    ok(Shared != NULL, "HeapAlloc failed\n");
    if (!Shared)
        return;
    Shared->Flags = 0;
    Shared->Length = ARRAYSIZE(Text) - 1;
    Shared->Ptr = Shared->Buffer;
    Shared->Count = 1;
    memcpy(Shared->Buffer, Text, sizeof(Text));
    String = (HSTRING)Shared;

    ok(WindowsGetStringLen(String) == ARRAYSIZE(Text) - 1, "WindowsGetStringLen returned %u\n", WindowsGetStringLen(String));
    ok(WindowsGetStringRawBuffer(String, NULL) == Shared->Buffer, "WindowsGetStringRawBuffer returned another buffer\n");
    hr = WindowsDuplicateString(String, &Copy);
    ok(hr == S_OK, "WindowsDuplicateString returned 0x%08lx\n", hr);
    ok(Copy == String, "Copy %p, string %p\n", Copy, String);
    ok(Shared->Count == 2, "Count %ld\n", Shared->Count);
    hr = WindowsDeleteString(Copy);
    ok(hr == S_OK, "WindowsDeleteString returned 0x%08lx\n", hr);
    ok(Shared->Count == 1, "Count %ld\n", Shared->Count);
    ok(HeapValidate(GetProcessHeap(), 0, Shared), "the string was freed early\n");
    hr = WindowsDeleteString(String);
    ok(hr == S_OK, "WindowsDeleteString returned 0x%08lx\n", hr);
    ok(HeapValidate(GetProcessHeap(), 0, NULL), "the process heap is corrupt\n");

    hr = WindowsCreateStringReference(Text, ARRAYSIZE(Text) - 1, &Header, &String);
    ok(hr == S_OK, "WindowsCreateStringReference returned 0x%08lx\n", hr);
    ok((((SHARED_HSTRING_HEADER *)String)->Flags & 1) != 0, "Flags %u\n", ((SHARED_HSTRING_HEADER *)String)->Flags);
    hr = WindowsDuplicateString(String, &Copy);
    ok(hr == S_OK, "WindowsDuplicateString returned 0x%08lx\n", hr);
    ok(Copy != String, "the reference was not copied\n");
    if (SUCCEEDED(hr) && Copy != String)
    {
        Shared = (SHARED_HSTRING_HEADER *)Copy;
        ok(Shared->Flags == 0, "Flags %u\n", Shared->Flags);
        ok(Shared->Count == 1, "Count %ld\n", Shared->Count);
        ok(HeapValidate(GetProcessHeap(), 0, Shared), "the copy is not a process heap block\n");
        WindowsDeleteString(Copy);
    }

    TestSizeLimit(MaxLength + 1);
    TestSizeLimit(MAXUINT32);

    Buffer = (HSTRING_BUFFER)0xdeadbeef;
    Chars = (WCHAR *)0xdeadbeef;
    hr = WindowsPreallocateStringBuffer(MaxLength, &Chars, &Buffer);
    ok(hr == S_OK || hr == E_OUTOFMEMORY, "WindowsPreallocateStringBuffer(%#x) returned 0x%08lx\n", MaxLength, hr);
    if (SUCCEEDED(hr))
        WindowsDeleteStringBuffer(Buffer);
    else
        ok(!Chars && !Buffer, "Chars %p, buffer %p\n", Chars, Buffer);

    String = (HSTRING)0xdeadbeef;
    hr = WindowsCreateString(NULL, ARRAYSIZE(Text) - 1, &String);
    ok(hr == E_POINTER, "WindowsCreateString(NULL) returned 0x%08lx\n", hr);
    ok(String == NULL, "String %p\n", String);

    if (sizeof(void *) < 8)
        return;
    Large = VirtualAlloc(NULL, (MaxLength / 2 + 2) * sizeof(WCHAR), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Large != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    if (!Large)
        return;
    hr = WindowsCreateStringReference(Large, MaxLength / 2 + 1, &Header, &String);
    ok(hr == S_OK, "WindowsCreateStringReference returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Copy = (HSTRING)0xdeadbeef;
        hr = WindowsConcatString(String, String, &Copy);
        ok(hr == MEM_E_INVALID_SIZE, "WindowsConcatString returned 0x%08lx\n", hr);
        ok(Copy == NULL, "Copy %p\n", Copy);
        if (SUCCEEDED(hr))
            WindowsDeleteString(Copy);
    }
    VirtualFree(Large, 0, MEM_RELEASE);
}
