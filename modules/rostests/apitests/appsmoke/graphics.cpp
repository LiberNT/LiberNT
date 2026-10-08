/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests device creation, rendering and presentation through Direct3D 11, Direct3D 12 and OpenGL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appsmoke.h"
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3d12.h>
#include <d3d11on12.h>

#define CHAIN_WIDTH 320
#define CHAIN_HEIGHT 240
#define GREEN_RGBA 0xFF00FF00u
#define BLUE_RGBA 0xFFFF0000u

#define APPSMOKE_GL_VENDOR 0x1F00
#define APPSMOKE_GL_RENDERER 0x1F01
#define APPSMOKE_GL_COLOR_BUFFER_BIT 0x4000
#define APPSMOKE_GL_RGBA 0x1908
#define APPSMOKE_GL_UNSIGNED_BYTE 0x1401

static bool ReadPixel(ID3D11Device *Device, ID3D11DeviceContext *Context, ID3D11Texture2D *Source, DWORD *Pixel)
{
    D3D11_MAPPED_SUBRESOURCE Map;
    ID3D11Texture2D *Staging = NULL;
    D3D11_TEXTURE2D_DESC Desc;
    bool Read;

    Source->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    if (FAILED(Device->CreateTexture2D(&Desc, NULL, &Staging)))
        return false;

    Context->CopyResource(Staging, Source);
    Read = SUCCEEDED(Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Map));
    if (Read)
    {
        *Pixel = *reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(Map.pData) + Map.RowPitch * 8 + 8 * 4);
        Context->Unmap(Staging, 0);
    }
    Staging->Release();
    return Read;
}

static UINT PresentFrames(IDXGISwapChain *Chain, ID3D11DeviceContext *Context, ID3D11RenderTargetView *View, UINT Count)
{
    static const float Green[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    UINT Index, Failures = 0;
    HRESULT Hr;

    for (Index = 0; Index < Count; ++Index)
    {
        Context->OMSetRenderTargets(1, &View, NULL);
        Context->ClearRenderTargetView(View, Green);
        Hr = Chain->Present(0, 0);
        if (Hr != S_OK && Hr != DXGI_STATUS_OCCLUDED)
        {
            if (!Failures)
                trace("Present %u returned %#lx\n", Index, Hr);
            ++Failures;
        }
    }
    return Failures;
}

static void TestChain(IDXGIFactory *Factory, ID3D11Device *Device, ID3D11DeviceContext *Context, UINT Samples, bool Fullscreen)
{
    static const float Green[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    HWND Window = AppSmokeCreateWindow(CHAIN_WIDTH, CHAIN_HEIGHT);
    ID3D11RenderTargetView *View = NULL;
    ID3D11Texture2D *Buffer = NULL, *Resolved = NULL;
    IDXGISwapChain *Chain = NULL;
    IDXGIOutput *Output = NULL;
    D3D11_TEXTURE2D_DESC Texture;
    DXGI_SWAP_CHAIN_DESC Desc;
    DXGI_MODE_DESC Target;
    DWORD Pixel = 0, Start;
    BOOL State = FALSE;
    RECT Client;
    UINT Failures;
    HRESULT Hr;

    ok(Window != NULL, "No window: %lu\n", GetLastError());
    if (!Window)
        return;

    memset(&Desc, 0, sizeof(Desc));
    Desc.BufferDesc.Width = CHAIN_WIDTH;
    Desc.BufferDesc.Height = CHAIN_HEIGHT;
    Desc.BufferDesc.RefreshRate.Numerator = 60;
    Desc.BufferDesc.RefreshRate.Denominator = 1;
    Desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = Samples;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 1;
    Desc.OutputWindow = Window;
    Desc.Windowed = TRUE;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    Hr = Factory->CreateSwapChain(Device, &Desc, &Chain);
    ok(Hr == S_OK && Chain, "%u-sample swap chain: %#lx\n", Samples, Hr);
    if (FAILED(Hr) || !Chain)
    {
        DestroyWindow(Window);
        return;
    }

    Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Buffer));
    ok(Hr == S_OK && Buffer, "%u-sample back buffer: %#lx\n", Samples, Hr);
    if (Buffer)
    {
        Hr = Device->CreateRenderTargetView(Buffer, NULL, &View);
        ok(Hr == S_OK && View, "%u-sample render target view: %#lx\n", Samples, Hr);
    }

    if (View)
    {
        Context->OMSetRenderTargets(1, &View, NULL);
        Context->ClearRenderTargetView(View, Green);
        if (Samples > 1)
        {
            Buffer->GetDesc(&Texture);
            Texture.SampleDesc.Count = 1;
            Texture.SampleDesc.Quality = 0;
            Texture.Usage = D3D11_USAGE_DEFAULT;
            Texture.BindFlags = D3D11_BIND_RENDER_TARGET;
            Texture.CPUAccessFlags = 0;
            Texture.MiscFlags = 0;
            Hr = Device->CreateTexture2D(&Texture, NULL, &Resolved);
            ok(Hr == S_OK && Resolved, "Resolve target: %#lx\n", Hr);
            if (Resolved)
                Context->ResolveSubresource(Resolved, 0, Buffer, 0, Desc.BufferDesc.Format);
        }

        ok(ReadPixel(Device, Context, Resolved ? Resolved : Buffer, &Pixel), "%u-sample back buffer readback failed\n", Samples);
        ok(Pixel == GREEN_RGBA, "%u-sample back buffer pixel is %#lx, expected %#x\n", Samples, Pixel, GREEN_RGBA);

        Start = GetTickCount();
        Failures = PresentFrames(Chain, Context, View, 8);
        ok(Failures == 0, "%u of 8 windowed presents failed with %u samples\n", Failures, Samples);
        trace("8 windowed presents with %u samples: %lu ms\n", Samples, GetTickCount() - Start);

        Target = Desc.BufferDesc;
        Target.Width = CHAIN_WIDTH + 80;
        Target.Height = CHAIN_HEIGHT + 60;
        Hr = Chain->ResizeTarget(&Target);
        ok(Hr == S_OK, "ResizeTarget: %#lx\n", Hr);
        AppSmokePumpMessages();
        GetClientRect(Window, &Client);
        ok(Client.right == (LONG)Target.Width && Client.bottom == (LONG)Target.Height,
           "The client area is %ldx%ld after ResizeTarget\n", Client.right, Client.bottom);
    }

    if (View && Fullscreen)
    {
        Hr = Chain->SetFullscreenState(TRUE, NULL);
        if (Hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE || Hr == DXGI_STATUS_MODE_CHANGE_IN_PROGRESS)
        {
            skip("Fullscreen is not available now: %#lx\n", Hr);
        }
        else
        {
            ok(Hr == S_OK, "Entering fullscreen with %u samples: %#lx\n", Samples, Hr);
            Hr = Chain->GetFullscreenState(&State, &Output);
            ok(Hr == S_OK && State && Output, "Fullscreen state: %#lx, %d, %p\n", Hr, State, Output);
            if (Output)
                Output->Release();

            Start = GetTickCount();
            Failures = PresentFrames(Chain, Context, View, 4);
            ok(Failures == 0, "%u of 4 fullscreen presents failed with %u samples\n", Failures, Samples);
            trace("4 fullscreen presents with %u samples: %lu ms\n", Samples, GetTickCount() - Start);

            Hr = Chain->SetFullscreenState(FALSE, NULL);
            ok(Hr == S_OK, "Leaving fullscreen: %#lx\n", Hr);
            State = TRUE;
            Hr = Chain->GetFullscreenState(&State, NULL);
            ok(Hr == S_OK && !State, "Windowed state: %#lx, %d\n", Hr, State);
        }
    }

    Context->OMSetRenderTargets(0, NULL, NULL);
    Context->ClearState();
    if (Resolved)
        Resolved->Release();
    if (View)
        View->Release();
    if (Buffer)
        Buffer->Release();
    Chain->Release();
    DestroyWindow(Window);
    AppSmokePumpMessages();
}

static void TestDefaultBufferMap(ID3D11Device *Device, ID3D11DeviceContext *Context)
{
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 Options;
    D3D11_MAPPED_SUBRESOURCE Map;
    ID3D11Buffer *Buffer = NULL;
    D3D11_BUFFER_DESC Desc;
    UINT Index, Wrong = 0;
    HRESULT Hr;

    memset(&Options, 0, sizeof(Options));
    Hr = Device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &Options, sizeof(Options));
    if (FAILED(Hr) || !Options.MapOnDefaultBuffers)
    {
        skip("Default-usage buffers are not mappable on this device: %#lx\n", Hr);
        return;
    }

    memset(&Desc, 0, sizeof(Desc));
    Desc.ByteWidth = 4096;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    Hr = Device->CreateBuffer(&Desc, NULL, &Buffer);
    ok(Hr == S_OK && Buffer, "Default-usage buffer with CPU access: %#lx\n", Hr);
    if (!Buffer)
        return;

    Hr = Context->Map(Buffer, 0, D3D11_MAP_WRITE, 0, &Map);
    ok(Hr == S_OK && Map.pData, "Mapping a default-usage buffer for writing: %#lx\n", Hr);
    if (Hr == S_OK && Map.pData)
    {
        for (Index = 0; Index < Desc.ByteWidth; ++Index)
            static_cast<BYTE *>(Map.pData)[Index] = static_cast<BYTE>(Index * 5 + 1);
        Context->Unmap(Buffer, 0);

        Hr = Context->Map(Buffer, 0, D3D11_MAP_READ, 0, &Map);
        ok(Hr == S_OK && Map.pData, "Mapping a default-usage buffer for reading: %#lx\n", Hr);
        if (Hr == S_OK && Map.pData)
        {
            for (Index = 0; Index < Desc.ByteWidth; ++Index)
            {
                if (static_cast<const BYTE *>(Map.pData)[Index] != static_cast<BYTE>(Index * 5 + 1))
                    ++Wrong;
            }
            ok(Wrong == 0, "%u of %u bytes read back from the default-usage buffer are wrong\n", Wrong, Desc.ByteWidth);
            Context->Unmap(Buffer, 0);
        }
    }
    Buffer->Release();
}

START_TEST(swap_chain)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
                                            UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    D3D_FEATURE_LEVEL Level = static_cast<D3D_FEATURE_LEVEL>(0);
    ID3D11ClassLinkage *Linkage = NULL;
    ID3D11DeviceContext *Context = NULL;
    ID3D11Device *Device = NULL;
    IDXGIDevice *DxgiDevice = NULL;
    IDXGIAdapter *Adapter = NULL;
    IDXGIFactory *Factory = NULL;
    WCHAR UmdName[MAX_PATH];
    bool HasUmd = AppSmokeD3D11DriverName(UmdName, ARRAYSIZE(UmdName));
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE Create;
    UINT Levels = 0;
    DWORD Start;
    HRESULT Hr;

    Create = Module ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice")) : NULL;
    if (!Create)
    {
        ok(!HasUmd, "d3d11.dll is unusable although the adapter has the Direct3D 11 driver %ls\n", UmdName);
        skip("D3D11CreateDevice is unavailable\n");
        return;
    }

    Start = GetTickCount();
    Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, &Level, &Context);
    if (FAILED(Hr) || !Device || !Context)
    {
        ok(!HasUmd, "No hardware device although the adapter has the Direct3D 11 driver %ls: %#lx\n", UmdName, Hr);
        skip("No hardware Direct3D 11 device: %#lx\n", Hr);
        return;
    }
    trace("Hardware device at feature level %#x in %lu ms\n", static_cast<UINT>(Level), GetTickCount() - Start);
    if (HasUmd)
        ok(AppSmokeModuleLoaded(UmdName), "The device does not run on the adapter's driver %ls\n", UmdName);

    if (Level >= D3D_FEATURE_LEVEL_11_0)
    {
        Hr = Device->CreateClassLinkage(&Linkage);
        ok(Hr == S_OK && Linkage, "CreateClassLinkage: %#lx\n", Hr);
        if (Linkage)
            Linkage->Release();
    }
    TestDefaultBufferMap(Device, Context);

    Hr = Device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void **>(&DxgiDevice));
    ok(Hr == S_OK && DxgiDevice, "IDXGIDevice: %#lx\n", Hr);
    if (DxgiDevice)
    {
        Hr = DxgiDevice->GetAdapter(&Adapter);
        ok(Hr == S_OK && Adapter, "Device adapter: %#lx\n", Hr);
    }
    if (Adapter)
    {
        Hr = Adapter->GetParent(IID_IDXGIFactory, reinterpret_cast<void **>(&Factory));
        ok(Hr == S_OK && Factory, "Adapter factory: %#lx\n", Hr);
    }

    if (Factory)
    {
        Hr = Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &Levels);
        ok(Hr == S_OK, "CheckMultisampleQualityLevels: %#lx\n", Hr);
        TestChain(Factory, Device, Context, 1, Levels == 0);
        if (Levels)
            TestChain(Factory, Device, Context, 4, true);
        else
            skip("The device has no 4-sample render targets\n");
        Factory->Release();
    }

    if (Adapter)
        Adapter->Release();
    if (DxgiDevice)
        DxgiDevice->Release();
    Context->Release();
    Device->Release();
}

static bool WaitForQueue(ID3D12CommandQueue *Queue, ID3D12Fence *Fence, UINT64 Value)
{
    HANDLE Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    DWORD Wait = WAIT_FAILED;
    HRESULT Hr;

    Hr = Queue->Signal(Fence, Value);
    ok(Hr == S_OK, "Queue signal %I64u: %#lx\n", Value, Hr);
    Hr = Fence->SetEventOnCompletion(Value, Event);
    ok(Hr == S_OK, "SetEventOnCompletion %I64u: %#lx\n", Value, Hr);
    if (SUCCEEDED(Hr))
        Wait = WaitForSingleObject(Event, 5000);
    ok(Wait == WAIT_OBJECT_0 && Fence->GetCompletedValue() >= Value,
       "The queue did not reach fence %I64u: wait %lu, completed %I64u\n", Value, Wait, Fence->GetCompletedValue());
    CloseHandle(Event);
    return Wait == WAIT_OBJECT_0;
}

static ID3D12Resource *CreateBuffer(ID3D12Device *Device, D3D12_HEAP_TYPE Type, D3D12_RESOURCE_STATES State)
{
    D3D12_HEAP_PROPERTIES Heap;
    D3D12_RESOURCE_DESC Desc;
    ID3D12Resource *Buffer = NULL;
    HRESULT Hr;

    memset(&Heap, 0, sizeof(Heap));
    Heap.Type = Type;
    memset(&Desc, 0, sizeof(Desc));
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = 4096;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Hr = Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, State, NULL,
                                         IID_ID3D12Resource, reinterpret_cast<void **>(&Buffer));
    ok(Hr == S_OK && Buffer, "Committed buffer on heap type %d: %#lx\n", Type, Hr);
    return Buffer;
}

static void TestOn12Wrap(ID3D12Device *Device, ID3D12CommandQueue *Queue, IDXGISwapChain1 *Chain)
{
    typedef HRESULT (WINAPI *CREATE_ON12)(IUnknown *, UINT, const D3D_FEATURE_LEVEL *, UINT, IUnknown *const *,
                                          UINT, UINT, ID3D11Device **, ID3D11DeviceContext **, D3D_FEATURE_LEVEL *);
    static const float Blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    CREATE_ON12 Create = Module ? reinterpret_cast<CREATE_ON12>(GetProcAddress(Module, "D3D11On12CreateDevice")) : NULL;
    D3D11_RESOURCE_FLAGS Flags = { D3D11_BIND_RENDER_TARGET, 0, 0, 0 };
    ID3D11RenderTargetView *View = NULL;
    ID3D11DeviceContext *Context11 = NULL;
    ID3D11Resource *Resources[1];
    ID3D12Resource *BackBuffer = NULL;
    ID3D11Texture2D *Wrapped = NULL;
    ID3D11On12Device *On12 = NULL;
    ID3D11Device *Device11 = NULL;
    IUnknown *Queues[1] = { Queue };
    HRESULT Hr;

    if (!Create)
    {
        skip("D3D11On12CreateDevice is unavailable\n");
        return;
    }

    Hr = Create(Device, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, Queues, 1, 0, &Device11, &Context11, NULL);
    ok(Hr == S_OK && Device11 && Context11, "D3D11On12CreateDevice: %#lx\n", Hr);
    if (Device11)
    {
        Hr = Device11->QueryInterface(IID_ID3D11On12Device, reinterpret_cast<void **>(&On12));
        ok(Hr == S_OK && On12, "ID3D11On12Device: %#lx\n", Hr);
    }
    Hr = Chain->GetBuffer(0, IID_ID3D12Resource, reinterpret_cast<void **>(&BackBuffer));
    ok(Hr == S_OK && BackBuffer, "Direct3D 12 back buffer: %#lx\n", Hr);

    if (On12 && BackBuffer)
    {
        Hr = On12->CreateWrappedResource(BackBuffer, &Flags, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT,
                                         IID_ID3D11Texture2D, reinterpret_cast<void **>(&Wrapped));
        ok(Hr == S_OK && Wrapped, "Wrapping a swap chain buffer for Direct3D 11: %#lx\n", Hr);
    }
    if (Wrapped)
    {
        Hr = Device11->CreateRenderTargetView(Wrapped, NULL, &View);
        ok(Hr == S_OK && View, "Render target view on the wrapped buffer: %#lx\n", Hr);
        if (View)
        {
            Resources[0] = Wrapped;
            On12->AcquireWrappedResources(Resources, 1);
            Context11->ClearRenderTargetView(View, Blue);
            On12->ReleaseWrappedResources(Resources, 1);
            Context11->Flush();
            Hr = Chain->Present(0, 0);
            ok(Hr == S_OK || Hr == DXGI_STATUS_OCCLUDED, "Present after drawing through Direct3D 11: %#lx\n", Hr);
            View->Release();
        }
        Wrapped->Release();
    }

    if (BackBuffer)
        BackBuffer->Release();
    if (On12)
        On12->Release();
    if (Context11)
    {
        Context11->ClearState();
        Context11->Flush();
        Context11->Release();
    }
    if (Device11)
        Device11->Release();
}

static void TestD3D12Present(ID3D12Device *Device, ID3D12CommandQueue *Queue, ID3D12Fence *Fence)
{
    typedef HRESULT (WINAPI *CREATE_FACTORY)(REFIID, void **);
    HMODULE Module = LoadLibraryW(L"dxgi.dll");
    CREATE_FACTORY Create = Module ? reinterpret_cast<CREATE_FACTORY>(GetProcAddress(Module, "CreateDXGIFactory1")) : NULL;
    IDXGIFactory2 *Factory = NULL;
    IDXGISwapChain1 *Chain = NULL;
    DXGI_SWAP_CHAIN_DESC1 Desc;
    UINT Index, Failures = 0;
    DWORD Start;
    HRESULT Hr;
    HWND Window;

    if (!Create || FAILED(Create(IID_IDXGIFactory2, reinterpret_cast<void **>(&Factory))) || !Factory)
    {
        skip("No IDXGIFactory2\n");
        return;
    }

    Window = AppSmokeCreateWindow(CHAIN_WIDTH, CHAIN_HEIGHT);
    memset(&Desc, 0, sizeof(Desc));
    Desc.Width = CHAIN_WIDTH;
    Desc.Height = CHAIN_HEIGHT;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 2;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Hr = Factory->CreateSwapChainForHwnd(Queue, Window, &Desc, NULL, NULL, &Chain);
    ok(Hr == S_OK && Chain, "Flip swap chain on the command queue: %#lx\n", Hr);
    if (Chain)
    {
        Start = GetTickCount();
        for (Index = 0; Index < 4; ++Index)
        {
            Hr = Chain->Present(0, 0);
            if (Hr != S_OK && Hr != DXGI_STATUS_OCCLUDED)
            {
                if (!Failures)
                    trace("Present %u returned %#lx\n", Index, Hr);
                ++Failures;
            }
        }
        ok(Failures == 0, "%u of 4 flip presents failed\n", Failures);
        WaitForQueue(Queue, Fence, 2);
        trace("4 flip presents: %lu ms\n", GetTickCount() - Start);
        TestOn12Wrap(Device, Queue, Chain);
        WaitForQueue(Queue, Fence, 3);
        Chain->Release();
    }

    Factory->Release();
    DestroyWindow(Window);
    AppSmokePumpMessages();
}

START_TEST(d3d12_device)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
    ID3D12Resource *Upload = NULL, *Readback = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList *List = NULL;
    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandList *Lists[1];
    ID3D12Device *Device = NULL;
    ID3D12Fence *Fence = NULL;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    WCHAR UmdName[MAX_PATH];
    bool HasUmd = AppSmokeD3D12DriverName(UmdName, ARRAYSIZE(UmdName));
    HMODULE Module = LoadLibraryW(L"d3d12.dll");
    CREATE_DEVICE Create;
    void *Data = NULL;
    UINT Index, Wrong = 0;
    DWORD Start;
    HRESULT Hr;

    Create = Module ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D12CreateDevice")) : NULL;
    if (!Create)
    {
        ok(!HasUmd, "d3d12.dll is unusable although the adapter has the Direct3D 12 driver %ls\n", UmdName);
        skip("D3D12CreateDevice is unavailable\n");
        return;
    }

    Start = GetTickCount();
    Hr = Create(NULL, D3D_FEATURE_LEVEL_11_0, IID_ID3D12Device, reinterpret_cast<void **>(&Device));
    if (FAILED(Hr) || !Device)
    {
        ok(!HasUmd, "No device although the adapter has the Direct3D 12 driver %ls: %#lx\n", UmdName, Hr);
        skip("No Direct3D 12 device: %#lx\n", Hr);
        return;
    }
    trace("Direct3D 12 device in %lu ms\n", GetTickCount() - Start);

    memset(&QueueDesc, 0, sizeof(QueueDesc));
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Hr = Device->CreateCommandQueue(&QueueDesc, IID_ID3D12CommandQueue, reinterpret_cast<void **>(&Queue));
    ok(Hr == S_OK && Queue, "Command queue: %#lx\n", Hr);
    Hr = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_ID3D12CommandAllocator,
                                        reinterpret_cast<void **>(&Allocator));
    ok(Hr == S_OK && Allocator, "Command allocator: %#lx\n", Hr);
    if (Allocator)
    {
        Hr = Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator, NULL,
                                       IID_ID3D12GraphicsCommandList, reinterpret_cast<void **>(&List));
        ok(Hr == S_OK && List, "Command list: %#lx\n", Hr);
    }
    Hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, reinterpret_cast<void **>(&Fence));
    ok(Hr == S_OK && Fence, "Fence: %#lx\n", Hr);

    Upload = CreateBuffer(Device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    Readback = CreateBuffer(Device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    if (Queue && List && Fence && Upload && Readback)
    {
        Hr = Upload->Map(0, NULL, &Data);
        ok(Hr == S_OK && Data, "Upload buffer map: %#lx\n", Hr);
        if (Data)
        {
            for (Index = 0; Index < 4096; ++Index)
                static_cast<BYTE *>(Data)[Index] = static_cast<BYTE>(Index * 7 + 3);
            Upload->Unmap(0, NULL);
        }

        List->CopyBufferRegion(Readback, 0, Upload, 0, 4096);
        Hr = List->Close();
        ok(Hr == S_OK, "Command list close: %#lx\n", Hr);
        Lists[0] = List;
        Queue->ExecuteCommandLists(1, Lists);
        if (WaitForQueue(Queue, Fence, 1))
        {
            Data = NULL;
            Hr = Readback->Map(0, NULL, &Data);
            ok(Hr == S_OK && Data, "Readback buffer map: %#lx\n", Hr);
            if (Data)
            {
                for (Index = 0; Index < 4096; ++Index)
                {
                    if (static_cast<const BYTE *>(Data)[Index] != static_cast<BYTE>(Index * 7 + 3))
                        ++Wrong;
                }
                ok(Wrong == 0, "%u of 4096 copied bytes are wrong\n", Wrong);
                Readback->Unmap(0, NULL);
            }
        }

        TestD3D12Present(Device, Queue, Fence);
    }

    if (Readback)
        Readback->Release();
    if (Upload)
        Upload->Release();
    if (Fence)
        Fence->Release();
    if (List)
        List->Release();
    if (Allocator)
        Allocator->Release();
    if (Queue)
        Queue->Release();
    Device->Release();
}

START_TEST(opengl)
{
    typedef HGLRC (WINAPI *CREATE_CONTEXT)(HDC);
    typedef BOOL (WINAPI *MAKE_CURRENT)(HDC, HGLRC);
    typedef BOOL (WINAPI *DELETE_CONTEXT)(HGLRC);
    typedef const unsigned char *(WINAPI *GET_STRING)(unsigned int);
    typedef void (WINAPI *CLEAR_COLOR)(float, float, float, float);
    typedef void (WINAPI *CLEAR)(unsigned int);
    typedef void (WINAPI *READ_PIXELS)(int, int, int, int, unsigned int, unsigned int, void *);
    typedef void (WINAPI *FINISH)(void);
    HMODULE Module = LoadLibraryW(L"opengl32.dll");
    PIXELFORMATDESCRIPTOR Descriptor;
    CREATE_CONTEXT CreateContext;
    DELETE_CONTEXT DeleteContext;
    MAKE_CURRENT MakeCurrent;
    READ_PIXELS ReadPixels;
    CLEAR_COLOR ClearColor;
    GET_STRING GetString;
    FINISH Finish;
    CLEAR Clear;
    const unsigned char *Vendor, *Renderer;
    WCHAR IcdName[MAX_PATH];
    bool HasIcd = AppSmokeOpenGlIcdName(IcdName, ARRAYSIZE(IcdName));
    UINT Index, Failures = 0;
    DWORD Pixel = 0, Start;
    HGLRC Context;
    HWND Window;
    int Format;
    HDC Dc;

    if (!Module)
    {
        skip("opengl32.dll is unavailable\n");
        return;
    }

    CreateContext = reinterpret_cast<CREATE_CONTEXT>(GetProcAddress(Module, "wglCreateContext"));
    MakeCurrent = reinterpret_cast<MAKE_CURRENT>(GetProcAddress(Module, "wglMakeCurrent"));
    DeleteContext = reinterpret_cast<DELETE_CONTEXT>(GetProcAddress(Module, "wglDeleteContext"));
    GetString = reinterpret_cast<GET_STRING>(GetProcAddress(Module, "glGetString"));
    ClearColor = reinterpret_cast<CLEAR_COLOR>(GetProcAddress(Module, "glClearColor"));
    Clear = reinterpret_cast<CLEAR>(GetProcAddress(Module, "glClear"));
    ReadPixels = reinterpret_cast<READ_PIXELS>(GetProcAddress(Module, "glReadPixels"));
    Finish = reinterpret_cast<FINISH>(GetProcAddress(Module, "glFinish"));
    ok(CreateContext && MakeCurrent && DeleteContext && GetString && ClearColor && Clear && ReadPixels && Finish,
       "opengl32.dll lacks a core entry point\n");
    if (!CreateContext || !MakeCurrent || !DeleteContext || !GetString || !ClearColor || !Clear || !ReadPixels || !Finish)
        return;

    Window = AppSmokeCreateWindow(256, 256);
    Dc = Window ? GetDC(Window) : NULL;
    ok(Dc != NULL, "No window DC: %lu\n", GetLastError());
    if (!Dc)
        return;

    memset(&Descriptor, 0, sizeof(Descriptor));
    Descriptor.nSize = sizeof(Descriptor);
    Descriptor.nVersion = 1;
    Descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    Descriptor.iPixelType = PFD_TYPE_RGBA;
    Descriptor.cColorBits = 32;
    Descriptor.cDepthBits = 24;
    Format = ChoosePixelFormat(Dc, &Descriptor);
    ok(Format != 0, "ChoosePixelFormat: %lu\n", GetLastError());
    ok(Format && SetPixelFormat(Dc, Format, &Descriptor), "SetPixelFormat: %lu\n", GetLastError());

    Start = GetTickCount();
    Context = Format ? CreateContext(Dc) : NULL;
    ok(Context != NULL, "wglCreateContext: %lu\n", GetLastError());
    if (Context && MakeCurrent(Dc, Context))
    {
        Vendor = GetString(APPSMOKE_GL_VENDOR);
        Renderer = GetString(APPSMOKE_GL_RENDERER);
        ok(Vendor && Renderer, "The context has no vendor or renderer string\n");
        trace("OpenGL %s / %s, context in %lu ms\n", Vendor ? reinterpret_cast<const char *>(Vendor) : "?",
              Renderer ? reinterpret_cast<const char *>(Renderer) : "?", GetTickCount() - Start);
        if (HasIcd)
            ok(AppSmokeModuleLoaded(IcdName), "The context does not run on the adapter's driver %ls\n", IcdName);

        Start = GetTickCount();
        for (Index = 0; Index < 8; ++Index)
        {
            ClearColor(0.0f, 0.0f, 1.0f, 1.0f);
            Clear(APPSMOKE_GL_COLOR_BUFFER_BIT);
            if (!Index)
            {
                ReadPixels(8, 8, 1, 1, APPSMOKE_GL_RGBA, APPSMOKE_GL_UNSIGNED_BYTE, &Pixel);
                ok(Pixel == BLUE_RGBA, "The cleared back buffer pixel is %#lx, expected %#x\n", Pixel, BLUE_RGBA);
            }
            if (!SwapBuffers(Dc))
                ++Failures;
        }
        Finish();
        ok(Failures == 0, "%u of 8 SwapBuffers calls failed\n", Failures);
        trace("8 OpenGL frames: %lu ms\n", GetTickCount() - Start);
        MakeCurrent(NULL, NULL);
    }
    else if (Context)
    {
        ok(0, "wglMakeCurrent: %lu\n", GetLastError());
    }

    if (Context)
        DeleteContext(Context);
    ReleaseDC(Window, Dc);
    DestroyWindow(Window);
    AppSmokePumpMessages();
}
