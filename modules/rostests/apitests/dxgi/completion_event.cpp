/* SPDX-License-Identifier: GPL-3.0-or-later
 * Public DXGI completion-event ordering and handle-lifetime regression. */
#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include "virtio3d.h"

START_TEST(completion_event)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
        UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
        D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    if (!Module) { skip("d3d11.dll is unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"));
    if (!Create) { skip("D3D11CreateDevice is unavailable\n"); FreeLibrary(Module); return; }
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    HRESULT Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                        D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(Hr)) { skip("Hardware D3D11 unavailable: %#lx\n", Hr); FreeLibrary(Module); return; }
    IDXGIDevice2 *Dxgi = NULL;
    Hr = Device->QueryInterface(IID_IDXGIDevice2, reinterpret_cast<void **>(&Dxgi));
    ok(SUCCEEDED(Hr), "IDXGIDevice2: %#lx\n", Hr);
    if (Dxgi)
    {
        ID3D11Texture2D *Texture = NULL;
        ID3D11RenderTargetView *Target = NULL;
        ID3D11Query *Query = NULL;
        D3D11_TEXTURE2D_DESC Desc = {};
        Desc.Width = 800; Desc.Height = 600; Desc.MipLevels = 1; Desc.ArraySize = 1;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_DEFAULT; Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        Hr = Device->CreateTexture2D(&Desc, NULL, &Texture);
        ok(SUCCEEDED(Hr), "CreateTexture2D: %#lx\n", Hr);
        if (Texture)
        {
            Hr = Device->CreateRenderTargetView(Texture, NULL, &Target);
            ok(SUCCEEDED(Hr), "CreateRenderTargetView: %#lx\n", Hr);
        }
        D3D11_QUERY_DESC QueryDesc = {D3D11_QUERY_EVENT, 0};
        Hr = Device->CreateQuery(&QueryDesc, &Query);
        ok(SUCCEEDED(Hr), "CreateQuery: %#lx\n", Hr);
        Hr = Dxgi->EnqueueSetEvent(NULL);
        ok(Hr == E_INVALIDARG, "NULL event: %#lx\n", Hr);
        Hr = Dxgi->EnqueueSetEvent(INVALID_HANDLE_VALUE);
        ok(Hr == E_INVALIDARG, "Invalid event: %#lx\n", Hr);
        UINT Latency = 0;
        Hr = Dxgi->GetMaximumFrameLatency(&Latency);
        ok(Hr == S_OK && Latency == 3, "Initial frame latency: %#lx, %u\n", Hr, Latency);
        Hr = Dxgi->SetMaximumFrameLatency(17);
        ok(Hr == DXGI_ERROR_INVALID_CALL, "Invalid frame latency: %#lx\n", Hr);
        Hr = Dxgi->SetMaximumFrameLatency(1);
        ok(SUCCEEDED(Hr), "Set frame latency: %#lx\n", Hr);
        Hr = Dxgi->GetMaximumFrameLatency(&Latency);
        ok(Hr == S_OK && Latency == 1, "Get frame latency: %#lx, %u\n", Hr, Latency);
        Hr = Dxgi->SetMaximumFrameLatency(0);
        ok(SUCCEEDED(Hr), "Restore default frame latency: %#lx\n", Hr);
        Hr = Dxgi->GetMaximumFrameLatency(&Latency);
        ok(Hr == S_OK && Latency == 0, "Frame latency after restoring the default: %#lx, %u\n", Hr, Latency);

        static const struct { DWORD Access; const char *Name; } Rights[] =
        {
            {EVENT_ALL_ACCESS, "all"},
            {EVENT_MODIFY_STATE, "modify"},
            {SYNCHRONIZE, "synchronize"},
            {EVENT_MODIFY_STATE | SYNCHRONIZE, "modify+synchronize"},
            {EVENT_ALL_ACCESS & ~0x0001, "all-query"},
            {EVENT_ALL_ACCESS & ~EVENT_MODIFY_STATE, "all-modify"},
            {EVENT_ALL_ACCESS & ~DELETE, "all-delete"},
            {EVENT_ALL_ACCESS & ~READ_CONTROL, "all-readcontrol"},
            {EVENT_ALL_ACCESS & ~WRITE_DAC, "all-writedac"},
            {EVENT_ALL_ACCESS & ~WRITE_OWNER, "all-writeowner"},
            {EVENT_ALL_ACCESS & ~SYNCHRONIZE, "all-synchronize"},
            {0x0001 | EVENT_MODIFY_STATE | SYNCHRONIZE, "query+modify+synchronize"},
        };
        for (UINT Right = 0; Right != ARRAYSIZE(Rights); ++Right)
        {
            HANDLE Event = CreateEventW(NULL, TRUE, FALSE, NULL), Limited = NULL;
            if (!Event) continue;
            if (DuplicateHandle(GetCurrentProcess(), Event, GetCurrentProcess(), &Limited, Rights[Right].Access, FALSE, 0))
            {
                Hr = Dxgi->EnqueueSetEvent(Limited);
                CloseHandle(Limited);
                if (Rights[Right].Access == EVENT_ALL_ACCESS)
                    ok(Hr == S_OK && WaitForSingleObject(Event, 5000) == WAIT_OBJECT_0,
                       "EnqueueSetEvent with all access: %#lx\n", Hr);
                else
                    ok(Hr == E_ACCESSDENIED, "EnqueueSetEvent with %s access: %#lx\n", Rights[Right].Name, Hr);
            }
            CloseHandle(Event);
        }

        if (Target && Query)
        {
            for (UINT Manual = 0; Manual != 2; ++Manual)
            {
                HANDLE Event = CreateEventW(NULL, Manual, FALSE, NULL);
                ok(Event != NULL, "CreateEvent: %lu\n", GetLastError());
                if (!Event) continue;
                for (UINT Frame = 0; Frame != 2; ++Frame)
                {
                    ResetEvent(Event);
                    for (UINT Draw = 0; Draw != 32; ++Draw)
                    {
                        FLOAT Color[4] = {Frame / 2.0f, Draw / 32.0f, 0.5f, 1.0f};
                        Context->ClearRenderTargetView(Target, Color);
                    }
                    Context->End(Query);
                    HANDLE Modify = NULL;
                    BOOL Duplicated = DuplicateHandle(GetCurrentProcess(), Event, GetCurrentProcess(),
                        &Modify, 0, FALSE, DUPLICATE_SAME_ACCESS);
                    ok(Duplicated, "DuplicateHandle: %lu\n", GetLastError());
                    if (!Duplicated) break;
                    /* Enqueue itself flushes, and must retain the event object
                     * after the caller closes the passed handle. */
                    Hr = Dxgi->EnqueueSetEvent(Modify);
                    CloseHandle(Modify);
                    ok(SUCCEEDED(Hr), "EnqueueSetEvent: %#lx\n", Hr);
                    if (FAILED(Hr)) break;
                    DWORD Wait = WaitForSingleObject(Event, 5000);
                    ok(Wait == WAIT_OBJECT_0, "Completion wait: %#lx\n", Wait);
                    if (Wait != WAIT_OBJECT_0) break;
                    BOOL Complete = FALSE;
                    Hr = Context->GetData(Query, &Complete, sizeof(Complete), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                    /* The host retires the event before the query on Windows 11 too. */
                    if (!DeviceIsVirtio3d(Device))
                    ok(Hr == S_OK && Complete, "Event preceded rendering completion: %#lx, %u\n", Hr, Complete);
                    Wait = WaitForSingleObject(Event, 0);
                    ok(Wait == (Manual ? WAIT_OBJECT_0 : WAIT_TIMEOUT), "Event reset semantics: %#lx\n", Wait);
                }
                CloseHandle(Event);
            }
        }
        if (Query) Query->Release();
        if (Target) Target->Release();
        if (Texture) Texture->Release();
        Dxgi->Release();
    }
    Context->ClearState();
    Context->Release();
    Device->Release();
    FreeLibrary(Module);
}
