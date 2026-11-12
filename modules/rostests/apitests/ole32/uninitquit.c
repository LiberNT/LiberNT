/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that uninitializing COM keeps a pending WM_QUIT for the thread's own loop
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <wine/test.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <objbase.h>
#include <ole2.h>

static void ok_quit(int expected)
{
    MSG msg;
    BOOL got = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);

    ok(got, "WM_QUIT was lost\n");
    if (got)
    {
        ok(msg.message == WM_QUIT, "got message %#x\n", msg.message);
        ok(msg.wParam == (WPARAM)expected, "got exit code %Iu\n", msg.wParam);
    }
}

START_TEST(uninitquit)
{
    IStream *stream = NULL;
    HRESULT hr;

    /* An apartment that marshaled an object owns a window whose pending calls are dispatched on the way out */
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(hr == S_OK, "CoInitializeEx returned %#lx\n", hr);
    hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    ok(hr == S_OK, "CreateStreamOnHGlobal returned %#lx\n", hr);
    if (stream)
    {
        hr = CoMarshalInterface(stream, &IID_IUnknown, (IUnknown *)stream, MSHCTX_INPROC, NULL, MSHLFLAGS_TABLESTRONG);
        ok(hr == S_OK, "CoMarshalInterface returned %#lx\n", hr);
    }
    PostQuitMessage(5);
    if (stream)
    {
        IStream_Seek(stream, (LARGE_INTEGER){0}, STREAM_SEEK_SET, NULL);
        CoReleaseMarshalData(stream);
        IStream_Release(stream);
    }
    CoUninitialize();
    ok_quit(5);

    hr = OleInitialize(NULL);
    ok(hr == S_OK, "OleInitialize returned %#lx\n", hr);
    PostQuitMessage(6);
    OleUninitialize();
    ok_quit(6);
}
