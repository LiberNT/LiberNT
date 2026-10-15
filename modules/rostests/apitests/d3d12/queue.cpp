/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Free-threaded command queue submission with per-wait fences
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

#define QUEUE_THREADS 4
#define QUEUE_SUBMISSIONS 2

struct queue_thread
{
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    UINT index;
    UINT completed;
    HRESULT failure;
};

static DWORD WINAPI QueueThread(void *parameter)
{
    struct queue_thread *thread = static_cast<struct queue_thread *>(parameter);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    ID3D12DescriptorHeap *heap = NULL;
    ID3D12Resource *target;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    HANDLE event = CreateEventA(NULL, FALSE, FALSE, NULL);
    FLOAT color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    HRESULT hr;

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors = 1;
    thread->device->CreateDescriptorHeap(&heap_desc, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void **>(&heap));
    thread->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&allocator));
    target = d3d12_test_create_target(thread->device, DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!heap || !allocator || !target)
    {
        thread->failure = E_FAIL;
        goto done;
    }
    rtv = heap->GetCPUDescriptorHandleForHeapStart();
    thread->device->CreateRenderTargetView(target, NULL, rtv);
    hr = thread->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
            __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(&list));
    if (FAILED(hr))
    {
        thread->failure = hr;
        goto done;
    }
    list->Close();
    for (UINT i = 0; i < QUEUE_SUBMISSIONS; ++i)
    {
        ID3D12CommandList *lists[1] = { list };
        ID3D12Fence *fence = NULL;

        color[0] = (FLOAT)thread->index / QUEUE_THREADS;
        allocator->Reset();
        list->Reset(allocator, NULL);
        list->ClearRenderTargetView(rtv, color, 0, NULL);
        list->Close();
        thread->queue->ExecuteCommandLists(1, lists);
        hr = thread->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(&fence));
        if (SUCCEEDED(hr)) hr = thread->queue->Signal(fence, 1);
        if (SUCCEEDED(hr)) hr = fence->SetEventOnCompletion(1, event);
        if (SUCCEEDED(hr) && WaitForSingleObject(event, D3D12_TEST_WAIT) != WAIT_OBJECT_0) hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        if (fence) fence->Release();
        if (FAILED(hr))
        {
            thread->failure = hr;
            break;
        }
        ++thread->completed;
    }
done:
    if (list) list->Release();
    if (target) target->Release();
    if (allocator) allocator->Release();
    if (heap) heap->Release();
    CloseHandle(event);
    return 0;
}

static void RunQueue(void *)
{
    struct d3d12_test_context context;
    struct queue_thread threads[QUEUE_THREADS] = {};
    HANDLE handles[QUEUE_THREADS];
    DWORD wait;

    if (!d3d12_test_init(&context)) return;
    for (UINT i = 0; i < QUEUE_THREADS; ++i)
    {
        threads[i].device = context.device;
        threads[i].queue = context.queue;
        threads[i].index = i;
        handles[i] = CreateThread(NULL, 0, QueueThread, &threads[i], 0, NULL);
        ok(handles[i] != NULL, "CreateThread %u failed\n", i);
    }
    wait = WaitForMultipleObjects(QUEUE_THREADS, handles, TRUE, D3D12_TEST_BUDGET);
    ok(wait == WAIT_OBJECT_0, "threads did not finish: %lu\n", wait);
    if (wait != WAIT_OBJECT_0) return;
    for (UINT i = 0; i < QUEUE_THREADS; ++i)
    {
        ok(threads[i].failure == S_OK, "thread %u failed with %#lx after %u iterations\n", i, threads[i].failure,
                threads[i].completed);
        ok(threads[i].completed == QUEUE_SUBMISSIONS, "thread %u completed %u iterations\n", i, threads[i].completed);
        if (handles[i]) CloseHandle(handles[i]);
    }
    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed: %#lx\n",
            context.device->GetDeviceRemovedReason());
    d3d12_test_destroy(&context);
}

START_TEST(queue)
{
    d3d12_test_bounded("queue", RunQueue, NULL, D3D12_TEST_BUDGET + D3D12_TEST_WAIT);
}
