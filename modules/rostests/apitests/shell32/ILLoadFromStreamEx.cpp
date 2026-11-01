/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ILLoadFromStreamEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"

static IStream *stream_from_bytes(const void *data, UINT size)
{
    return SHCreateMemStream((const BYTE *)data, size);
}

static void test_load(IStream *stream, HRESULT expected_hr, BOOL expect_pidl, PCSTR what)
{
    PIDLIST_RELATIVE pidl = (PIDLIST_RELATIVE)(ULONG_PTR)0xdeadbeef;
    HRESULT hr;

    hr = ILLoadFromStreamEx(stream, &pidl);
    ok(hr == expected_hr, "%s: hr 0x%lx, expected 0x%lx\n", what, hr, expected_hr);
    if (expect_pidl)
        ok(pidl != NULL && pidl != (PIDLIST_RELATIVE)(ULONG_PTR)0xdeadbeef, "%s: pidl %p\n", what, pidl);
    else
        ok(pidl == NULL, "%s: pidl %p\n", what, pidl);
    if (pidl && pidl != (PIDLIST_RELATIVE)(ULONG_PTR)0xdeadbeef)
        ILFree(pidl);
}

START_TEST(ILLoadFromStreamEx)
{
    PIDLIST_ABSOLUTE pidl;
    PIDLIST_RELATIVE loaded;
    IStream *stream;
    LARGE_INTEGER zero;
    HRESULT hr;
    BYTE buffer[64];

    CoInitialize(NULL);

    pidl = ILCreateFromPathW(L"C:\\Windows");
    ok(pidl != NULL, "ILCreateFromPathW failed\n");
    if (!pidl)
        return;

    stream = SHCreateMemStream(NULL, 0);
    hr = ILSaveToStream(stream, pidl);
    ok(hr == S_OK, "ILSaveToStream hr 0x%lx\n", hr);
    zero.QuadPart = 0;
    stream->Seek(zero, STREAM_SEEK_SET, NULL);

    loaded = (PIDLIST_RELATIVE)(ULONG_PTR)0xdeadbeef;
    hr = ILLoadFromStreamEx(stream, &loaded);
    ok(hr == S_OK, "roundtrip hr 0x%lx\n", hr);
    ok(loaded && loaded != (PIDLIST_RELATIVE)(ULONG_PTR)0xdeadbeef && ILIsEqual(pidl, (PCIDLIST_ABSOLUTE)loaded),
       "roundtrip pidl mismatch\n");
    if (loaded && loaded != (PIDLIST_RELATIVE)(ULONG_PTR)0xdeadbeef)
        ILFree(loaded);

    test_load(stream, HRESULT_FROM_WIN32(ERROR_NOT_FOUND), FALSE, "end of stream");
    stream->Release();

    stream = stream_from_bytes(NULL, 0);
    test_load(stream, HRESULT_FROM_WIN32(ERROR_NOT_FOUND), FALSE, "empty stream");
    stream->Release();

    ZeroMemory(buffer, sizeof(buffer));
    stream = stream_from_bytes(buffer, 2);
    test_load(stream, HRESULT_FROM_WIN32(ERROR_NOT_FOUND), FALSE, "zero length");
    stream->Release();

    ZeroMemory(buffer, sizeof(buffer));
    stream = stream_from_bytes(buffer, 4);
    test_load(stream, HRESULT_FROM_WIN32(ERROR_NOT_FOUND), FALSE, "zero length with terminator");
    stream->Release();

    buffer[0] = 2;
    buffer[1] = 0;
    buffer[2] = 0;
    buffer[3] = 0;
    stream = stream_from_bytes(buffer, 4);
    test_load(stream, S_OK, TRUE, "empty pidl");
    stream->Release();

    buffer[0] = 40;
    buffer[1] = 0;
    stream = stream_from_bytes(buffer, 12);
    test_load(stream, S_OK, TRUE, "truncated after terminator");
    stream->Release();

    ZeroMemory(buffer, sizeof(buffer));
    buffer[0] = 8;
    buffer[2] = 30;
    stream = stream_from_bytes(buffer, 10);
    test_load(stream, E_FAIL, FALSE, "corrupt item size");
    stream->Release();

    ZeroMemory(buffer, sizeof(buffer));
    buffer[0] = 40;
    buffer[2] = 6;
    buffer[4] = 'a';
    buffer[5] = 'b';
    buffer[6] = 'c';
    buffer[7] = 'd';
    stream = stream_from_bytes(buffer, 8);
    test_load(stream, S_OK, TRUE, "truncated without terminator");
    stream->Release();

    ZeroMemory(buffer, sizeof(buffer));
    buffer[0] = 8;
    buffer[2] = 6;
    buffer[4] = 'a';
    buffer[5] = 'b';
    buffer[6] = 'c';
    buffer[7] = 'd';
    stream = stream_from_bytes(buffer, 10);
    test_load(stream, S_OK, TRUE, "exact single item");
    stream->Release();

    ZeroMemory(buffer, sizeof(buffer));
    buffer[0] = 6;
    buffer[2] = 6;
    buffer[4] = 'a';
    buffer[5] = 'b';
    buffer[6] = 'c';
    buffer[7] = 'd';
    stream = stream_from_bytes(buffer, 16);
    test_load(stream, E_FAIL, FALSE, "length excludes terminator");
    stream->Release();

    ZeroMemory(buffer, sizeof(buffer));
    buffer[0] = 1;
    stream = stream_from_bytes(buffer, 8);
    test_load(stream, E_FAIL, FALSE, "length 1");
    stream->Release();

    ILFree(pidl);
    CoUninitialize();
}
