/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Source region of Direct3D 12 window swap chains
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "d3d12_apitest.h"

#define SWAPCHAIN_WIDTH 128u
#define SWAPCHAIN_HEIGHT 96u
#define SOURCE_WIDTH 64u
#define SOURCE_HEIGHT 48u

static void CheckSourceSize(IDXGISwapChain3 *chain, UINT width, UINT height, HRESULT expected,
        UINT expected_width, UINT expected_height)
{
    UINT actual_width = ~0u, actual_height = ~0u;
    HRESULT hr = chain->SetSourceSize(width, height);
    HRESULT query = chain->GetSourceSize(&actual_width, &actual_height);

    ok(hr == expected, "SetSourceSize(%u, %u) = %#lx, expected %#lx\n", width, height, hr, expected);
    ok(query == S_OK && actual_width == expected_width && actual_height == expected_height,
       "After SetSourceSize(%u, %u): GetSourceSize %#lx %u x %u, expected %u x %u\n", width, height, query,
       actual_width, actual_height, expected_width, expected_height);
}

static BOOL ScreenColorNear(HDC screen, POINT origin, LONG x, LONG y, COLORREF expected)
{
    COLORREF pixel = GetPixel(screen, origin.x + x, origin.y + y);

    return pixel != CLR_INVALID &&
           abs((int)GetRValue(pixel) - (int)GetRValue(expected)) <= 8 &&
           abs((int)GetGValue(pixel) - (int)GetGValue(expected)) <= 8 &&
           abs((int)GetBValue(pixel) - (int)GetBValue(expected)) <= 8;
}

static BOOL WaitForScreenColor(HDC screen, POINT origin, LONG x, LONG y, COLORREF expected)
{
    DWORD start = GetTickCount();

    do
    {
        if (ScreenColorNear(screen, origin, x, y, expected))
            return TRUE;
        Sleep(10);
    } while (GetTickCount() - start < 2000);
    return FALSE;
}

/* The top-left source quadrant is red, the rest of the back buffer blue. */
static BOOL PresentQuadrant(struct d3d12_test_context *context, IDXGISwapChain3 *chain)
{
    static const FLOAT blue[4] = {0.0f, 0.0f, 1.0f, 1.0f}, red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    D3D12_RECT quadrant = {0, 0, (LONG)SOURCE_WIDTH, (LONG)SOURCE_HEIGHT};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    ID3D12Resource *buffer = NULL;
    HRESULT hr;

    hr = chain->GetBuffer(chain->GetCurrentBackBufferIndex(), __uuidof(ID3D12Resource),
                          reinterpret_cast<void **>(&buffer));
    ok(hr == S_OK && buffer, "GetBuffer: %#lx\n", hr);
    if (!buffer)
        return FALSE;
    context->device->CreateRenderTargetView(buffer, NULL, rtv);
    hr = context->allocator->Reset();
    ok(hr == S_OK, "allocator Reset: %#lx\n", hr);
    hr = context->list->Reset(context->allocator, NULL);
    ok(hr == S_OK, "list Reset: %#lx\n", hr);
    d3d12_test_transition(context->list, buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    context->list->ClearRenderTargetView(rtv, blue, 0, NULL);
    context->list->ClearRenderTargetView(rtv, red, 1, &quadrant);
    d3d12_test_transition(context->list, buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    buffer->Release();
    if (!d3d12_test_execute(context, context->list, D3D12_TEST_WAIT))
        return FALSE;
    hr = chain->Present(0, 0);
    ok(hr == S_OK, "Present: %#lx\n", hr);
    return hr == S_OK;
}

START_TEST(swapchain)
{
    struct d3d12_test_context context;
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    IDXGISwapChain1 *chain1 = NULL;
    IDXGISwapChain3 *chain = NULL;
    IDXGIFactory2 *factory = NULL;
    POINT origin = {340, 300};
    UINT width = ~0u, height = ~0u;
    HWND window;
    HDC screen;
    HRESULT hr;

    if (!d3d12_test_init(&context))
    {
        skip("No D3D12 device\n");
        return;
    }
    hr = context.list->Close();
    ok(hr == S_OK, "Close the initial list: %#lx\n", hr);
    hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), reinterpret_cast<void **>(&factory));
    ok(hr == S_OK, "CreateDXGIFactory2: %#lx\n", hr);
    window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"STATIC", L"d3d12 swapchain", WS_POPUP | WS_VISIBLE,
                             origin.x, origin.y, SWAPCHAIN_WIDTH, SWAPCHAIN_HEIGHT, NULL, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow: %lu\n", GetLastError());
    screen = GetDC(NULL);
    if (factory && window && screen)
    {
        desc.Width = SWAPCHAIN_WIDTH;
        desc.Height = SWAPCHAIN_HEIGHT;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        hr = factory->CreateSwapChainForHwnd(context.queue, window, &desc, NULL, NULL, &chain1);
        ok(hr == S_OK && chain1, "CreateSwapChainForHwnd: %#lx\n", hr);
        if (chain1)
            hr = chain1->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void **>(&chain));
        ok(hr == S_OK && chain, "IDXGISwapChain3: %#lx\n", hr);
    }
    if (chain)
    {
        hr = chain->GetSourceSize(&width, &height);
        ok(hr == S_OK && width == SWAPCHAIN_WIDTH && height == SWAPCHAIN_HEIGHT,
           "Initial source %#lx %u x %u\n", hr, width, height);
        CheckSourceSize(chain, SWAPCHAIN_WIDTH + 1, SWAPCHAIN_HEIGHT, DXGI_ERROR_INVALID_CALL,
                        SWAPCHAIN_WIDTH, SWAPCHAIN_HEIGHT);
        CheckSourceSize(chain, 0, SWAPCHAIN_HEIGHT, S_OK, 0, SWAPCHAIN_HEIGHT);
        CheckSourceSize(chain, 0, 0, S_OK, 0, 0);
        CheckSourceSize(chain, SOURCE_WIDTH, SOURCE_HEIGHT, S_OK, SOURCE_WIDTH, SOURCE_HEIGHT);
        if (PresentQuadrant(&context, chain))
            ok(WaitForScreenColor(screen, origin, SWAPCHAIN_WIDTH * 3 / 4, SWAPCHAIN_HEIGHT * 3 / 4, RGB(255, 0, 0)),
               "The source region was not stretched over the window\n");
        CheckSourceSize(chain, SWAPCHAIN_WIDTH, SWAPCHAIN_HEIGHT, S_OK, SWAPCHAIN_WIDTH, SWAPCHAIN_HEIGHT);
        if (PresentQuadrant(&context, chain))
            ok(WaitForScreenColor(screen, origin, SWAPCHAIN_WIDTH * 3 / 4, SWAPCHAIN_HEIGHT * 3 / 4, RGB(0, 0, 255)),
               "The full source never reached the screen\n");
        CheckSourceSize(chain, SOURCE_WIDTH, SOURCE_HEIGHT, S_OK, SOURCE_WIDTH, SOURCE_HEIGHT);
        d3d12_test_wait_queue(context.device, context.queue, D3D12_TEST_WAIT);
        hr = chain->ResizeBuffers(0, SWAPCHAIN_WIDTH, SWAPCHAIN_HEIGHT, DXGI_FORMAT_UNKNOWN, 0);
        ok(hr == S_OK, "ResizeBuffers: %#lx\n", hr);
        width = height = ~0u;
        hr = chain->GetSourceSize(&width, &height);
        ok(hr == S_OK && width == SWAPCHAIN_WIDTH && height == SWAPCHAIN_HEIGHT,
           "After ResizeBuffers: GetSourceSize %#lx %u x %u\n", hr, width, height);
        chain->Release();
    }
    if (chain1) chain1->Release();
    if (screen) ReleaseDC(NULL, screen);
    if (window) DestroyWindow(window);
    if (factory) factory->Release();
    d3d12_test_destroy(&context);
}
