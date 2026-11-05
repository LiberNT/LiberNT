/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A process blocked acquiring a keyed mutex can still be terminated
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <stdio.h>
#include <initguid.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>

#define KILL_READY_MS 30000
#define KILL_SETTLE_MS 500
#define KILL_EXIT_MS 5000
#define KILL_EXIT_CODE 7

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static ID3D11Device *CreateDevice(void)
{
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE Create = Module ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice")) : NULL;
    D3D_FEATURE_LEVEL Level = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device *Device = NULL;

    if (Create)
        Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &Level, 1, D3D11_SDK_VERSION, &Device, NULL, NULL);
    return Device;
}

static UINT RunChild(HANDLE Shared, HANDLE Ready)
{
    ID3D11Device *Device = CreateDevice();
    ID3D11Device1 *Device1 = NULL;
    ID3D11Texture2D *Texture = NULL;
    IDXGIKeyedMutex *Mutex = NULL;
    HRESULT Hr;

    if (!Device)
        return 2;
    if (FAILED(Device->QueryInterface(IID_ID3D11Device1, reinterpret_cast<void **>(&Device1))))
        return 3;
    if (FAILED(Device1->OpenSharedResource1(Shared, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Texture))))
        return 4;
    if (FAILED(Texture->QueryInterface(IID_IDXGIKeyedMutex, reinterpret_cast<void **>(&Mutex))))
        return 5;
    SetEvent(Ready);
    Hr = Mutex->AcquireSync(1, INFINITE);
    return Hr == S_OK ? 6 : 8;
}

static void TestTerminateBlockedAcquire(ID3D11Device *Device)
{
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    D3D11_TEXTURE2D_DESC Desc = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                  D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0,
                                  D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE };
    PROCESS_INFORMATION Info = {};
    STARTUPINFOA Startup = {};
    ID3D11Texture2D *Texture = NULL;
    IDXGIKeyedMutex *Mutex = NULL;
    IDXGIResource1 *Resource = NULL;
    HANDLE Shared = NULL, Ready = NULL, Objects[2];
    char Command[MAX_PATH + 96];
    char **Argv;
    DWORD Wait, Start, ExitCode = 0xdeadbeef;
    HRESULT Hr;

    Hr = Device->CreateTexture2D(&Desc, NULL, &Texture);
    ok(Hr == S_OK, "CreateTexture2D(keyed mutex, NT handle): %#lx\n", Hr);
    if (!Texture)
        return;
    Hr = Texture->QueryInterface(IID_IDXGIKeyedMutex, reinterpret_cast<void **>(&Mutex));
    ok(Hr == S_OK, "IDXGIKeyedMutex: %#lx\n", Hr);
    Hr = Texture->QueryInterface(IID_IDXGIResource1, reinterpret_cast<void **>(&Resource));
    ok(Hr == S_OK, "IDXGIResource1: %#lx\n", Hr);
    if (!Mutex || !Resource)
        goto done;
    Hr = Mutex->AcquireSync(0, INFINITE);
    ok(Hr == S_OK, "AcquireSync(0): %#lx\n", Hr);
    Hr = Resource->CreateSharedHandle(&Attributes, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &Shared);
    ok(Hr == S_OK, "CreateSharedHandle(inheritable): %#lx\n", Hr);
    Ready = CreateEventA(&Attributes, TRUE, FALSE, NULL);
    if (!Shared || !Ready)
        goto done;
    winetest_get_mainargs(&Argv);
    snprintf(Command, sizeof(Command), "\"%s\" keyedmutex_kill child %Ix %Ix", Argv[0],
             reinterpret_cast<ULONG_PTR>(Shared), reinterpret_cast<ULONG_PTR>(Ready));
    Startup.cb = sizeof(Startup);
    ok(CreateProcessA(NULL, Command, NULL, NULL, TRUE, 0, NULL, NULL, &Startup, &Info),
       "CreateProcess failed: %lu\n", GetLastError());
    if (!Info.hProcess)
        goto done;
    Objects[0] = Ready;
    Objects[1] = Info.hProcess;
    Wait = WaitForMultipleObjects(2, Objects, FALSE, KILL_READY_MS);
    ok(Wait == WAIT_OBJECT_0, "child did not reach AcquireSync: %lu\n", Wait);
    if (Wait == WAIT_OBJECT_0)
    {
        Sleep(KILL_SETTLE_MS);
        ok(WaitForSingleObject(Info.hProcess, 0) == WAIT_TIMEOUT, "child acquired a key nobody released\n");
        Start = GetTickCount();
        ok(TerminateProcess(Info.hProcess, KILL_EXIT_CODE), "TerminateProcess failed: %lu\n", GetLastError());
        Wait = WaitForSingleObject(Info.hProcess, KILL_EXIT_MS);
        trace("child blocked in AcquireSync exited %lu ms after TerminateProcess\n", GetTickCount() - Start);
        ok(Wait == WAIT_OBJECT_0, "child blocked in AcquireSync did not exit after TerminateProcess: %lu\n", Wait);
    }
    Hr = Mutex->ReleaseSync(1);
    ok(Hr == S_OK, "ReleaseSync(1) after the waiter died: %#lx\n", Hr);
    if (WaitForSingleObject(Info.hProcess, KILL_EXIT_MS) != WAIT_OBJECT_0)
        TerminateProcess(Info.hProcess, 1);
    GetExitCodeProcess(Info.hProcess, &ExitCode);
    ok(ExitCode == KILL_EXIT_CODE, "child exit code %lu\n", ExitCode);
    Hr = Mutex->AcquireSync(1, 1000);
    ok(Hr == S_OK, "AcquireSync(1) after the waiter died: %#lx\n", Hr);
    if (Hr == S_OK)
        Mutex->ReleaseSync(0);
    CloseHandle(Info.hThread);
    CloseHandle(Info.hProcess);
done:
    if (Ready)
        CloseHandle(Ready);
    if (Shared)
        CloseHandle(Shared);
    if (Resource)
        Resource->Release();
    if (Mutex)
        Mutex->Release();
    Texture->Release();
}

START_TEST(keyedmutex_kill)
{
    ID3D11Device *Device;
    char **Argv;
    int Argc = winetest_get_mainargs(&Argv);

    if (Argc >= 5 && !strcmp(Argv[2], "child"))
        ExitProcess(RunChild(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(_strtoui64(Argv[3], NULL, 16))),
                             reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(_strtoui64(Argv[4], NULL, 16)))));
    Device = CreateDevice();
    if (!Device)
    {
        skip("no hardware device\n");
        return;
    }
    TestTerminateBlockedAcquire(Device);
    Device->Release();
}
