/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Command allocator and command list release order (DirectX-Specs CPUEfficiency)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

#include <stdio.h>

static ULONG RefCount(IUnknown *object)
{
    object->AddRef();
    return object->Release();
}

struct release_order
{
    ID3D12Device *device;
    D3D12_COMMAND_LIST_TYPE type;
    BOOL close;
    BOOL allocator_first;
};

static void TestReleaseOrder(void *parameter)
{
    const struct release_order *order = static_cast<const struct release_order *>(parameter);
    ID3D12Device *device = order->device;
    D3D12_COMMAND_LIST_TYPE type = order->type;
    BOOL close = order->close;
    BOOL allocator_first = order->allocator_first;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    ULONG count;
    HRESULT hr;

    hr = device->CreateCommandAllocator(type, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void **>(&allocator));
    ok(hr == S_OK, "type %u: CreateCommandAllocator: %#lx\n", type, hr);
    if (FAILED(hr)) return;
    hr = device->CreateCommandList(0, type, allocator, NULL, __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void **>(&list));
    ok(hr == S_OK, "type %u: CreateCommandList: %#lx\n", type, hr);
    if (FAILED(hr))
    {
        allocator->Release();
        return;
    }
    count = RefCount(allocator);
    ok(count == 1, "type %u: allocator public reference count %lu\n", type, count);
    if (close) ok(list->Close() == S_OK, "type %u: Close failed\n", type);
    if (allocator_first)
    {
        count = allocator->Release();
        ok(count == 0, "type %u: allocator release returned %lu\n", type, count);
        count = list->Release();
        ok(count == 0, "type %u: list release returned %lu\n", type, count);
    }
    else
    {
        count = list->Release();
        ok(count == 0, "type %u: list release returned %lu\n", type, count);
        count = allocator->Release();
        ok(count == 0, "type %u: allocator release returned %lu\n", type, count);
    }
}

static void TestResetToNewAllocator(void *parameter)
{
    struct d3d12_test_context *context = static_cast<struct d3d12_test_context *>(parameter);
    ID3D12CommandAllocator *first = NULL, *second = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    HRESULT hr;

    context->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&first));
    context->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&second));
    if (!first || !second) goto done;
    hr = context->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, first, NULL,
            __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(&list));
    ok(hr == S_OK, "CreateCommandList: %#lx\n", hr);
    if (FAILED(hr)) goto done;
    ok(list->Close() == S_OK, "Close failed\n");
    first->Release();
    first = NULL;
    hr = list->Reset(second, NULL);
    ok(hr == S_OK, "Reset to a new allocator after the old one was released: %#lx\n", hr);
    ok(d3d12_test_execute(context, list, D3D12_TEST_WAIT), "execution after the allocator switch did not complete\n");
    hr = second->Reset();
    ok(hr == S_OK, "allocator Reset after completion: %#lx\n", hr);
done:
    if (list) list->Release();
    if (first) first->Release();
    if (second) second->Release();
}

START_TEST(allocator)
{
    static const D3D12_COMMAND_LIST_TYPE types[] =
    {
        D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_BUNDLE,
        D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY,
    };
    struct d3d12_test_context *context = static_cast<struct d3d12_test_context *>(
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*context)));
    struct release_order *orders = static_cast<struct release_order *>(
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ARRAYSIZE(types) * 4 * sizeof(*orders)));
    char name[64];

    if (!context || !orders || !d3d12_test_init(context)) return;
    for (UINT i = 0; i < ARRAYSIZE(types) * 4; ++i)
    {
        struct release_order *order = &orders[i];
        order->device = context->device;
        order->type = types[i / 4];
        order->close = (i & 1) != 0;
        order->allocator_first = (i & 2) == 0;
        sprintf(name, "release order type %u close %d allocator first %d", order->type, order->close,
                order->allocator_first);
        if (!d3d12_test_bounded(name, TestReleaseOrder, order, D3D12_TEST_WAIT)) return;
    }
    ok(context->list->Close() == S_OK, "Close(initial list)\n");
    if (!d3d12_test_bounded("reset to a new allocator", TestResetToNewAllocator, context, D3D12_TEST_BUDGET)) return;
    d3d12_test_destroy(context);
    HeapFree(GetProcessHeap(), 0, orders);
    HeapFree(GetProcessHeap(), 0, context);
}
