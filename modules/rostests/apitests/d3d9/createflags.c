/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDirect3D9::CreateDevice checks the device type and the vertex processing flags
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <wine/test.h>

START_TEST(createflags)
{
    static const struct
    {
        const char *name;
        D3DDEVTYPE type;
        DWORD flags;
        HRESULT expected;
    }
    tests[] =
    {
        { "hardware", D3DDEVTYPE_HAL, D3DCREATE_HARDWARE_VERTEXPROCESSING, D3D_OK },
        { "software", D3DDEVTYPE_HAL, D3DCREATE_SOFTWARE_VERTEXPROCESSING, D3D_OK },
        { "mixed", D3DDEVTYPE_HAL, D3DCREATE_MIXED_VERTEXPROCESSING, D3D_OK },
        { "pure hardware", D3DDEVTYPE_HAL, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE, D3D_OK },
        { "type 0", (D3DDEVTYPE)0, D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DERR_INVALIDCALL },
        { "software type", D3DDEVTYPE_SW, D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DERR_INVALIDCALL },
        { "type 5", (D3DDEVTYPE)5, D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DERR_INVALIDCALL },
        { "no vertex processing", D3DDEVTYPE_HAL, 0, D3DERR_INVALIDCALL },
        { "hardware and software", D3DDEVTYPE_HAL,
          D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_SOFTWARE_VERTEXPROCESSING, D3DERR_INVALIDCALL },
        { "all vertex processing", D3DDEVTYPE_HAL, D3DCREATE_HARDWARE_VERTEXPROCESSING
          | D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING, D3DERR_INVALIDCALL },
        { "pure software", D3DDEVTYPE_HAL, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
          D3DERR_INVALIDCALL },
        { "pure mixed", D3DDEVTYPE_HAL, D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_PUREDEVICE, D3DERR_INVALIDCALL },
        { "pure alone", D3DDEVTYPE_HAL, D3DCREATE_PUREDEVICE, D3DERR_INVALIDCALL },
    };
    D3DPRESENT_PARAMETERS parameters;
    IDirect3DDevice9 *device;
    IDirect3D9 *d3d;
    HRESULT hr;
    HWND window;
    UINT i;

    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    ok(d3d != NULL, "Direct3DCreate9 failed\n");
    if (!d3d)
        return;
    window = CreateWindowW(L"static", L"createflags", WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, NULL, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow failed\n");

    for (i = 0; i < ARRAYSIZE(tests); i++)
    {
        memset(&parameters, 0, sizeof(parameters));
        parameters.Windowed = TRUE;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.hDeviceWindow = window;
        device = NULL;
        hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, tests[i].type, window, tests[i].flags, &parameters,
                                     &device);
        ok(hr == tests[i].expected, "%s: CreateDevice returned %#lx\n", tests[i].name, hr);
        ok(!device == FAILED(tests[i].expected), "%s: device %p\n", tests[i].name, device);
        if (device)
            IDirect3DDevice9_Release(device);
    }

    DestroyWindow(window);
    IDirect3D9_Release(d3d);
}
