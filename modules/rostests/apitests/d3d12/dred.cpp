/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DRED auto-breadcrumbs and page fault data after device removal
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static const GUID debug_object_name_w = {0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41}};

static ID3D12Resource *CreateBuffer(ID3D12Device *device, UINT64 size)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *resource = NULL;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "CreateCommittedResource: %#lx\n", hr);
    return resource;
}

static void CheckBreadcrumbs(const D3D12_AUTO_BREADCRUMB_NODE1 *head, ID3D12GraphicsCommandList *list,
        ID3D12GraphicsCommandList *completed, ID3D12GraphicsCommandList *copy, ID3D12CommandQueue *queue)
{
    static const D3D12_AUTO_BREADCRUMB_OP expected[] =
    {
        D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT,
        D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION,
        D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER,
        D3D12_AUTO_BREADCRUMB_OP_SETMARKER,
        D3D12_AUTO_BREADCRUMB_OP_ENDEVENT,
    };
    const D3D12_AUTO_BREADCRUMB_NODE1 *node, *found = NULL, *done = NULL;
    UINT nodes = 0, matched = 0;

    if (!head)
    {
        skip("no auto-breadcrumb data on this adapter\n");
        return;
    }

    for (node = head; node && nodes < 64; node = node->pNext, ++nodes)
    {
        trace("node %u: list %p (%s) queue %p (%s), %u breadcrumbs, last %u\n", nodes, node->pCommandList,
                node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "(null)", node->pCommandQueue,
                node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA : "(null)", node->BreadcrumbCount,
                node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : ~0u);
        for (UINT i = 0; node->pCommandHistory && i < node->BreadcrumbCount && i < 16; ++i)
            trace("  op %u: %u\n", i, node->pCommandHistory[i]);
        if (node->pCommandList == list) found = node;
        if (node->pCommandList == completed) done = node;
        ok(node->pCommandList != copy, "the copy command list has a breadcrumb node\n");
    }
    ok(done != NULL, "no breadcrumb node for the completed command list (%u nodes)\n", nodes);
    if (done)
        ok(done->BreadcrumbCount == 1 && done->pLastBreadcrumbValue && *done->pLastBreadcrumbValue == 1
                && done->pCommandHistory && done->pCommandHistory[0] == D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION,
                "completed list: %u breadcrumbs, last %u\n", done->BreadcrumbCount,
                done->pLastBreadcrumbValue ? *done->pLastBreadcrumbValue : ~0u);
    ok(found != NULL, "no breadcrumb node for the pending command list (%u nodes)\n", nodes);
    if (!found) return;
    ok(found->pCommandQueue == queue, "node queue %p, expected %p\n", found->pCommandQueue, queue);
    ok(found->pCommandListDebugNameW && !wcscmp(found->pCommandListDebugNameW, L"dred list"), "list name W %s\n",
            wine_dbgstr_w(found->pCommandListDebugNameW));
    ok(found->pCommandListDebugNameA && !strcmp(found->pCommandListDebugNameA, "dred list"), "list name A %s\n",
            found->pCommandListDebugNameA ? found->pCommandListDebugNameA : "(null)");
    ok(found->pCommandQueueDebugNameW && !wcscmp(found->pCommandQueueDebugNameW, L"dred queue"), "queue name W %s\n",
            wine_dbgstr_w(found->pCommandQueueDebugNameW));
    ok(found->pLastBreadcrumbValue && !*found->pLastBreadcrumbValue, "completed %u of %u breadcrumbs\n",
            found->pLastBreadcrumbValue ? *found->pLastBreadcrumbValue : ~0u, found->BreadcrumbCount);
    ok(found->pCommandHistory != NULL, "no command history\n");
    for (UINT i = 0; found->pCommandHistory && i < found->BreadcrumbCount && matched < ARRAYSIZE(expected); ++i)
        if (found->pCommandHistory[i] == expected[matched]) ++matched;
    ok(matched == ARRAYSIZE(expected), "matched %u of %u recorded operations\n", matched,
            static_cast<UINT>(ARRAYSIZE(expected)));
}

static void RunDred(void *)
{
    ID3D12DeviceRemovedExtendedDataSettings1 *settings = NULL;
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs1 = {};
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
    D3D12_DRED_PAGE_FAULT_OUTPUT1 fault1 = {};
    ID3D12DeviceRemovedExtendedData1 *dred = NULL;
    D3D12_RESOURCE_BARRIER barrier = {};
    struct d3d12_test_context context;
    ID3D12Resource *source = NULL, *destination = NULL;
    ID3D12GraphicsCommandList *completed = NULL, *copy = NULL;
    ID3D12CommandAllocator *allocator = NULL, *copy_allocator = NULL;
    D3D12_COMMAND_QUEUE_DESC copy_desc = {};
    ID3D12CommandQueue *copy_queue = NULL;
    ID3D12Fence *blocker = NULL;
    ID3D12Device5 *device5 = NULL;
    WCHAR name[32] = {};
    UINT size;
    HRESULT hr;

    hr = D3D12GetDebugInterface(__uuidof(ID3D12DeviceRemovedExtendedDataSettings1), reinterpret_cast<void **>(&settings));
    ok(hr == S_OK && settings, "DRED settings: %#lx\n", hr);
    if (!settings) return;
    settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    if (!d3d12_test_init(&context))
    {
        settings->Release();
        return;
    }

    hr = context.list->SetName(L"dred list");
    ok(hr == S_OK, "SetName: %#lx\n", hr);
    size = sizeof(name);
    hr = context.list->GetPrivateData(debug_object_name_w, &size, name);
    ok(hr == S_OK && size == sizeof(L"dred list") && !wcscmp(name, L"dred list"),
            "debug name private data: %#lx, size %u, %s\n", hr, size, wine_dbgstr_w(name));
    hr = context.queue->SetName(L"dred queue");
    ok(hr == S_OK, "queue SetName: %#lx\n", hr);

    source = CreateBuffer(context.device, 256);
    destination = CreateBuffer(context.device, 256);
    hr = context.device->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedData1), reinterpret_cast<void **>(&dred));
    ok(hr == S_OK && dred, "ID3D12DeviceRemovedExtendedData1: %#lx\n", hr);
    hr = context.device->QueryInterface(__uuidof(ID3D12Device5), reinterpret_cast<void **>(&device5));
    ok(hr == S_OK && device5, "ID3D12Device5: %#lx\n", hr);
    hr = context.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&allocator));
    ok(hr == S_OK, "CreateCommandAllocator: %#lx\n", hr);
    hr = context.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&blocker));
    ok(hr == S_OK, "CreateFence: %#lx\n", hr);
    copy_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    hr = context.device->CreateCommandQueue(&copy_desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&copy_queue));
    ok(hr == S_OK, "copy queue: %#lx\n", hr);
    hr = context.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&copy_allocator));
    ok(hr == S_OK, "copy allocator: %#lx\n", hr);
    if (source && destination && dred && device5 && allocator && blocker && copy_queue && copy_allocator)
    {
        hr = context.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, copy_allocator, NULL,
                __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(&copy));
        ok(hr == S_OK, "copy list: %#lx\n", hr);
        if (copy)
        {
            ID3D12CommandList *lists[] = { copy };

            copy->CopyBufferRegion(destination, 0, source, 0, 256);
            ok(copy->Close() == S_OK, "copy list Close failed\n");
            copy_queue->ExecuteCommandLists(1, lists);
            ok(d3d12_test_wait_queue(context.device, copy_queue, D3D12_TEST_WAIT), "the copy list did not complete\n");
        }

        context.list->Close();
        context.allocator->Reset();
        hr = context.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context.allocator, NULL,
                __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(&completed));
        ok(hr == S_OK, "CreateCommandList: %#lx\n", hr);
        if (completed)
        {
            completed->SetName(L"completed list");
            completed->CopyBufferRegion(destination, 0, source, 0, 256);
            ok(d3d12_test_execute(&context, completed, D3D12_TEST_WAIT), "the completed list did not complete\n");
        }

        hr = context.list->Reset(allocator, NULL);
        ok(hr == S_OK, "Reset: %#lx\n", hr);
        context.list->BeginEvent(1, "event", sizeof("event"));
        context.list->CopyBufferRegion(destination, 0, source, 0, 256);
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        context.list->ResourceBarrier(1, &barrier);
        context.list->SetMarker(1, "marker", sizeof("marker"));
        context.list->EndEvent();
        hr = context.list->Close();
        ok(hr == S_OK, "Close: %#lx\n", hr);
        hr = context.queue->Wait(blocker, 1);
        ok(hr == S_OK, "queue Wait: %#lx\n", hr);
        {
            ID3D12CommandList *lists[] = { context.list };
            context.queue->ExecuteCommandLists(1, lists);
        }

        hr = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs1);
        ok(hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE, "breadcrumbs before removal: %#lx\n", hr);
        hr = dred->GetPageFaultAllocationOutput1(&fault1);
        ok(hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE, "page fault before removal: %#lx\n", hr);

        device5->RemoveDevice();
        hr = context.device->GetDeviceRemovedReason();
        ok(hr == DXGI_ERROR_DEVICE_REMOVED, "removed reason %#lx\n", hr);

        hr = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs1);
        ok(hr == S_OK, "breadcrumbs after removal: %#lx\n", hr);
        if (hr == S_OK)
            CheckBreadcrumbs(breadcrumbs1.pHeadAutoBreadcrumbNode, context.list, completed, copy, context.queue);
        hr = dred->GetAutoBreadcrumbsOutput(&breadcrumbs);
        ok(hr == S_OK, "breadcrumbs 1.1 after removal: %#lx\n", hr);
        ok(reinterpret_cast<const void *>(breadcrumbs.pHeadAutoBreadcrumbNode)
                == reinterpret_cast<const void *>(breadcrumbs1.pHeadAutoBreadcrumbNode) || !breadcrumbs1.pHeadAutoBreadcrumbNode,
                "breadcrumbs 1.1 head %p, 1.2 head %p\n", breadcrumbs.pHeadAutoBreadcrumbNode,
                breadcrumbs1.pHeadAutoBreadcrumbNode);
        hr = dred->GetPageFaultAllocationOutput1(&fault1);
        ok(hr == S_OK, "page fault after removal: %#lx\n", hr);
        ok(!fault1.PageFaultVA, "page fault VA %#I64x without a fault\n", fault1.PageFaultVA);
        trace("existing nodes %p, freed nodes %p\n", fault1.pHeadExistingAllocationNode,
                fault1.pHeadRecentFreedAllocationNode);
        blocker->Signal(1);
        trace("queue drained after removal: %d\n", d3d12_test_wait_queue(context.device, context.queue, D3D12_TEST_WAIT));
    }
    if (copy) copy->Release();
    if (copy_allocator) copy_allocator->Release();
    if (copy_queue) copy_queue->Release();
    if (completed) completed->Release();
    if (blocker) blocker->Release();
    if (allocator) allocator->Release();
    if (device5) device5->Release();
    if (dred) dred->Release();
    if (destination) destination->Release();
    if (source) source->Release();
    d3d12_test_destroy(&context);
    settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED);
    settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED);
    settings->Release();
}

START_TEST(dred)
{
    d3d12_test_bounded("dred", RunDred, NULL, D3D12_TEST_BUDGET);
}
