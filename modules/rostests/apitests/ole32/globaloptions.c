/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that IGlobalOptions keeps the values it is given
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <wine/test.h>
#include <windef.h>
#include <winbase.h>
#include <objbase.h>

START_TEST(globaloptions)
{
    IGlobalOptions *Options;
    ULONG_PTR Value;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_GlobalOptions, NULL, CLSCTX_INPROC_SERVER, &IID_IGlobalOptions, (void **)&Options);
    ok(hr == S_OK, "CoCreateInstance failed, hr %#lx\n", hr);
    if (FAILED(hr))
    {
        CoUninitialize();
        return;
    }

    hr = IGlobalOptions_Set(Options, COMGLB_EXCEPTION_HANDLING, COMGLB_EXCEPTION_DONOT_HANDLE);
    ok(hr == S_OK, "Set(COMGLB_EXCEPTION_HANDLING) failed, hr %#lx\n", hr);
    Value = 0xdead;
    hr = IGlobalOptions_Query(Options, COMGLB_EXCEPTION_HANDLING, &Value);
    ok(hr == S_OK, "Query(COMGLB_EXCEPTION_HANDLING) failed, hr %#lx\n", hr);
    ok(Value == COMGLB_EXCEPTION_DONOT_HANDLE, "Exception handling is %Iu\n", Value);

    hr = IGlobalOptions_Set(Options, COMGLB_RO_SETTINGS, COMGLB_FAST_RUNDOWN);
    ok(hr == S_OK, "Set(COMGLB_RO_SETTINGS) failed, hr %#lx\n", hr);
    Value = 0xdead;
    hr = IGlobalOptions_Query(Options, COMGLB_RO_SETTINGS, &Value);
    ok(hr == S_OK, "Query(COMGLB_RO_SETTINGS) failed, hr %#lx\n", hr);
    ok(Value == COMGLB_FAST_RUNDOWN, "RO settings are %Iu\n", Value);

    hr = IGlobalOptions_Query(Options, 0, &Value);
    ok(FAILED(hr), "Query(0) succeeded\n");

    IGlobalOptions_Release(Options);
    CoUninitialize();
}
