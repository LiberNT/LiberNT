/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 resources and fences shared under a name
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>

typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                         UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

typedef struct _TEST_UNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} TEST_UNICODE_STRING;

typedef LONG (WINAPI *PNT_QUERY_OBJECT)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static void CheckObjectName(PCSTR What, HANDLE Handle, PCWSTR Leaf)
{
    PNT_QUERY_OBJECT Query = reinterpret_cast<PNT_QUERY_OBJECT>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject"));
    union { TEST_UNICODE_STRING Name; BYTE Bytes[1024]; } Buffer;
    WCHAR Name[260] = L"";
    ULONG Length = 0;

    if (Query && Query(Handle, 1, &Buffer, sizeof(Buffer), &Length) >= 0 && Buffer.Name.Buffer)
        lstrcpynW(Name, Buffer.Name.Buffer, min((ULONG)ARRAYSIZE(Name), (ULONG)Buffer.Name.Length / sizeof(WCHAR) + 1));
    WCHAR Expected[128];
    DWORD Session = 0;

    ProcessIdToSessionId(GetCurrentProcessId(), &Session);
    wsprintfW(Expected, L"\\Sessions\\%lu\\BaseNamedObjects\\%s", Session, Leaf);
    ok(!lstrcmpW(Name, Expected), "%s object %ls, expected %ls\n", What, Name, Expected);
}

static ID3D11Device1 *CreateDevice1(CREATE_DEVICE Create)
{
    ID3D11Device *Device = NULL;
    ID3D11Device1 *Device1 = NULL;

    if (FAILED(Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, NULL)))
        return NULL;
    Device->QueryInterface(IID_ID3D11Device1, reinterpret_cast<void **>(&Device1));
    Device->Release();
    return Device1;
}

START_TEST(shared_name)
{
    ID3D11Device1 *First, *Second;
    ID3D11Device5 *First5 = NULL;
    ID3D11Texture2D *Texture = NULL, *Opened = NULL;
    IDXGIResource1 *Resource = NULL;
    ID3D11Fence *Fence = NULL;
    D3D11_TEXTURE2D_DESC Desc = {}, OpenedDesc = {};
    HANDLE Handle = NULL, Again = (HANDLE)0xdeadbeef, FenceHandle = NULL;
    WCHAR Name[64], FenceName[64];
    HRESULT Hr;

    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    if (!Module) { skip("d3d11.dll is unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"));
    First = Create ? CreateDevice1(Create) : NULL;
    Second = Create ? CreateDevice1(Create) : NULL;
    if (!First || !Second)
    {
        skip("two D3D11.1 devices are needed\n");
        if (First) First->Release();
        if (Second) Second->Release();
        FreeLibrary(Module);
        return;
    }
    wsprintfW(Name, L"lnt-shared-texture-%lu", GetCurrentProcessId());
    wsprintfW(FenceName, L"lnt-shared-fence-%lu", GetCurrentProcessId());

    Desc.Width = 64; Desc.Height = 32; Desc.MipLevels = 1; Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; Desc.SampleDesc.Count = 1; Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    Hr = First->CreateTexture2D(&Desc, NULL, &Texture);
    ok(Hr == S_OK, "CreateTexture2D(shared NT handle): %#lx\n", Hr);
    if (Texture)
        Texture->QueryInterface(IID_IDXGIResource1, reinterpret_cast<void **>(&Resource));
    if (Resource)
    {
        Hr = Resource->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, Name, &Handle);
        ok(Hr == S_OK, "CreateSharedHandle(named texture): %#lx\n", Hr);
        if (Handle) CheckObjectName("named texture", Handle, Name);
        Hr = Resource->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, Name, &Again);
        ok(Hr == DXGI_ERROR_NAME_ALREADY_EXISTS, "CreateSharedHandle(same texture name): %#lx\n", Hr);
        ok(Again == NULL, "CreateSharedHandle(same texture name) left handle %p\n", Again);
        if (SUCCEEDED(Hr) && Again && Again != (HANDLE)0xdeadbeef) CloseHandle(Again);
    }
    if (Handle)
    {
        Hr = Second->OpenSharedResourceByName(Name, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                              IID_ID3D11Texture2D, reinterpret_cast<void **>(&Opened));
        ok(Hr == S_OK, "OpenSharedResourceByName: %#lx\n", Hr);
        if (Opened)
        {
            Opened->GetDesc(&OpenedDesc);
            ok(OpenedDesc.Width == 64 && OpenedDesc.Height == 32, "opened texture is %ux%u\n", OpenedDesc.Width, OpenedDesc.Height);
            Opened->Release();
            Opened = NULL;
        }
        Hr = Second->OpenSharedResourceByName(L"lnt-shared-texture-missing", DXGI_SHARED_RESOURCE_READ,
                                              IID_ID3D11Texture2D, reinterpret_cast<void **>(&Opened));
        ok(Hr == E_INVALIDARG, "OpenSharedResourceByName(missing): %#lx\n", Hr);
        if (Opened) Opened->Release();
        CloseHandle(Handle);
    }

    if (SUCCEEDED(First->QueryInterface(IID_ID3D11Device5, reinterpret_cast<void **>(&First5))))
    {
        Hr = First5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_ID3D11Fence, reinterpret_cast<void **>(&Fence));
        ok(Hr == S_OK, "CreateFence(SHARED): %#lx\n", Hr);
        if (Fence)
        {
            Hr = Fence->CreateSharedHandle(NULL, GENERIC_ALL, FenceName, &FenceHandle);
            ok(Hr == S_OK, "CreateSharedHandle(named fence): %#lx\n", Hr);
            if (FenceHandle)
            {
                HANDLE FenceAgain = (HANDLE)0xdeadbeef;

                CheckObjectName("named fence", FenceHandle, FenceName);
                Hr = Fence->CreateSharedHandle(NULL, GENERIC_ALL, FenceName, &FenceAgain);
                ok(Hr == DXGI_ERROR_NAME_ALREADY_EXISTS, "CreateSharedHandle(same fence name): %#lx\n", Hr);
                ok(FenceAgain == (HANDLE)0xdeadbeef, "CreateSharedHandle(same fence name) wrote handle %p\n", FenceAgain);
                if (SUCCEEDED(Hr) && FenceAgain && FenceAgain != (HANDLE)0xdeadbeef) CloseHandle(FenceAgain);
                CloseHandle(FenceHandle);
            }
            Fence->Release();
        }
        First5->Release();
    }

    if (Resource) Resource->Release();
    if (Texture) Texture->Release();
    Second->Release();
    First->Release();
    FreeLibrary(Module);
}
