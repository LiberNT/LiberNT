/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 fences shared through NT handles
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "d3d12_apitest.h"
#include <stdio.h>

static ID3D12CommandQueue *CreateQueue(ID3D12Device *device)
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    ID3D12CommandQueue *queue = NULL;

    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT hr = device->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&queue));
    ok(hr == S_OK, "CreateCommandQueue: %#lx\n", hr);
    return queue;
}

static BOOL WaitFence(ID3D12Fence *fence, UINT64 value, DWORD timeout)
{
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);
    HRESULT hr = fence->SetEventOnCompletion(value, event);
    BOOL reached = SUCCEEDED(hr) && WaitForSingleObject(event, timeout) == WAIT_OBJECT_0;

    ok(hr == S_OK, "SetEventOnCompletion(%I64u): %#lx\n", value, hr);
    CloseHandle(event);
    return reached;
}

static void TestShareRules(ID3D12Device *device)
{
    ID3D12Fence *fence = NULL;
    HANDLE handle = (HANDLE)0xdeadbeef;
    HRESULT hr;

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence));
    ok(hr == S_OK, "CreateFence: %#lx\n", hr);
    if (!fence) return;
    hr = device->CreateSharedHandle(fence, NULL, GENERIC_ALL, NULL, &handle);
    ok(hr == E_INVALIDARG, "CreateSharedHandle(unshared fence): %#lx\n", hr);
    ok(handle == (HANDLE)0xdeadbeef, "unshared fence wrote handle %p\n", handle);
    if (SUCCEEDED(hr) && handle) CloseHandle(handle);
    fence->Release();
}

static void TestSameProcess(ID3D12Device *device)
{
    ID3D12Fence *fence = NULL, *opened = NULL, *marker = NULL;
    ID3D12Fence1 *opened1 = NULL;
    ID3D12Resource *resource = NULL;
    IUnknown *unknown = NULL;
    ID3D12CommandQueue *queue;
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), NULL, TRUE };
    HANDLE handle = NULL, inheritable = NULL, event;
    DWORD handle_flags = 0;
    HRESULT hr;

    hr = device->CreateFence(3, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence));
    ok(hr == S_OK, "CreateFence(SHARED): %#lx\n", hr);
    if (!fence) return;
    hr = device->CreateSharedHandle(fence, NULL, GENERIC_ALL, NULL, &handle);
    ok(hr == S_OK, "CreateSharedHandle: %#lx\n", hr);
    if (FAILED(hr) || !handle)
    {
        fence->Release();
        return;
    }

    hr = device->OpenSharedHandle(handle, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&opened));
    ok(hr == S_OK, "OpenSharedHandle(ID3D12Fence): %#lx\n", hr);
    if (opened)
    {
        ok(opened != fence, "the opened fence is the creating object\n");
        ok(opened->GetCompletedValue() == 3, "opened fence reads %I64u, created at 3\n", opened->GetCompletedValue());
        hr = fence->Signal(5);
        ok(hr == S_OK, "creator Signal(5): %#lx\n", hr);
        ok(opened->GetCompletedValue() == 5, "opened fence reads %I64u after 5\n", opened->GetCompletedValue());
        hr = opened->Signal(6);
        ok(hr == S_OK, "opened Signal(6): %#lx\n", hr);
        ok(fence->GetCompletedValue() == 6, "creator reads %I64u after the opened fence signalled 6\n", fence->GetCompletedValue());
        if (SUCCEEDED(opened->QueryInterface(__uuidof(ID3D12Fence1), reinterpret_cast<void **>(&opened1))))
        {
            ok(opened1->GetCreationFlags() == D3D12_FENCE_FLAG_SHARED, "opened fence flags %#x\n", opened1->GetCreationFlags());
            opened1->Release();
        }

        event = CreateEventW(NULL, FALSE, FALSE, NULL);
        hr = opened->SetEventOnCompletion(7, event);
        ok(hr == S_OK, "opened SetEventOnCompletion(7): %#lx\n", hr);
        ok(WaitForSingleObject(event, 0) == WAIT_TIMEOUT, "event set before 7\n");
        fence->Signal(7);
        ok(WaitForSingleObject(event, 2000) == WAIT_OBJECT_0, "creator signal did not release the opened fence's event\n");
        CloseHandle(event);

        queue = CreateQueue(device);
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&marker));
        ok(hr == S_OK, "CreateFence(marker): %#lx\n", hr);
        if (queue && marker)
        {
            hr = queue->Wait(opened, 8);
            ok(hr == S_OK, "queue Wait(opened, 8): %#lx\n", hr);
            hr = queue->Signal(marker, 1);
            ok(hr == S_OK, "queue Signal(marker): %#lx\n", hr);
            Sleep(300);
            ok(marker->GetCompletedValue() == 0, "queue passed an unsatisfied wait on the opened fence\n");
            fence->Signal(8);
            ok(WaitFence(marker, 1, 5000), "creator signal did not release the queue's wait on the opened fence\n");
        }
        if (marker) marker->Release();
        if (queue) queue->Release();
        opened->Release();
    }

    hr = device->OpenSharedHandle(handle, __uuidof(IUnknown), reinterpret_cast<void **>(&unknown));
    ok(hr == S_OK, "OpenSharedHandle(IUnknown): %#lx\n", hr);
    if (unknown)
    {
        hr = unknown->QueryInterface(__uuidof(ID3D12Fence), reinterpret_cast<void **>(&opened));
        ok(hr == S_OK, "IUnknown from a fence handle is not a fence: %#lx\n", hr);
        if (opened) opened->Release();
        unknown->Release();
    }
    hr = device->OpenSharedHandle(handle, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == E_NOINTERFACE, "OpenSharedHandle(fence as ID3D12Resource): %#lx\n", hr);
    if (resource) resource->Release();
    CloseHandle(handle);

    hr = device->CreateSharedHandle(fence, &attributes, GENERIC_ALL, NULL, &inheritable);
    ok(hr == S_OK, "CreateSharedHandle(inheritable): %#lx\n", hr);
    if (inheritable)
    {
        ok(GetHandleInformation(inheritable, &handle_flags), "GetHandleInformation failed: %lu\n", GetLastError());
        ok(handle_flags & HANDLE_FLAG_INHERIT, "shared handle flags %#lx, expected inheritable\n", handle_flags);
        CloseHandle(inheritable);
    }
    fence->Release();
}

static void RunChild(ID3D12Device *device, HANDLE handle)
{
    ID3D12Fence *fence = NULL;
    ID3D12CommandQueue *queue;
    HRESULT hr;

    hr = device->OpenSharedHandle(handle, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence));
    ok(hr == S_OK, "child OpenSharedHandle: %#lx\n", hr);
    if (!fence) return;
    ok(fence->GetCompletedValue() == 1, "child reads %I64u, parent created at 1\n", fence->GetCompletedValue());
    hr = fence->Signal(10);
    ok(hr == S_OK, "child Signal(10): %#lx\n", hr);
    queue = CreateQueue(device);
    if (queue)
    {
        hr = queue->Wait(fence, 20);
        ok(hr == S_OK, "child queue Wait(20): %#lx\n", hr);
        hr = queue->Signal(fence, 30);
        ok(hr == S_OK, "child queue Signal(30): %#lx\n", hr);
        ok(WaitFence(fence, 30, 10000), "child queue did not reach 30\n");
        queue->Release();
    }
    fence->Release();
}

static void TestOtherProcess(ID3D12Device *device)
{
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), NULL, TRUE };
    PROCESS_INFORMATION info = {};
    STARTUPINFOA startup = {};
    ID3D12Fence *fence = NULL;
    HANDLE handle = NULL;
    char command[MAX_PATH + 64];
    char **argv;
    DWORD exit_code = 0xdeadbeef;
    HRESULT hr;

    hr = device->CreateFence(1, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence));
    ok(hr == S_OK, "CreateFence(SHARED): %#lx\n", hr);
    if (!fence) return;
    hr = device->CreateSharedHandle(fence, &attributes, GENERIC_ALL, NULL, &handle);
    ok(hr == S_OK, "CreateSharedHandle(inheritable): %#lx\n", hr);
    if (!handle)
    {
        skip("no inheritable fence handle for the child\n");
        fence->Release();
        return;
    }
    winetest_get_mainargs(&argv);
    snprintf(command, sizeof(command), "\"%s\" sharedfence child %Ix", argv[0], reinterpret_cast<ULONG_PTR>(handle));
    startup.cb = sizeof(startup);
    ok(CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &info),
       "CreateProcess failed: %lu\n", GetLastError());
    if (info.hProcess)
    {
        ok(WaitFence(fence, 10, 10000), "child CPU signal did not reach the parent\n");
        hr = fence->Signal(20);
        ok(hr == S_OK, "parent Signal(20): %#lx\n", hr);
        ok(WaitFence(fence, 30, 10000), "child GPU signal after its wait did not reach the parent\n");
        if (WaitForSingleObject(info.hProcess, 20000) != WAIT_OBJECT_0)
        {
            ok(FALSE, "child did not exit\n");
            TerminateProcess(info.hProcess, 1);
        }
        GetExitCodeProcess(info.hProcess, &exit_code);
        ok(exit_code == 0, "child reported %lu failures\n", exit_code);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
    }
    CloseHandle(handle);
    fence->Release();
}

START_TEST(sharedfence)
{
    ID3D12Device *device;
    char **argv;
    int argc = winetest_get_mainargs(&argv);

    device = d3d12_test_create_device();
    if (!device) return;
    if (argc >= 4 && !strcmp(argv[2], "child"))
    {
        RunChild(device, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(_strtoui64(argv[3], NULL, 16))));
        device->Release();
        return;
    }
    TestShareRules(device);
    TestSameProcess(device);
    TestOtherProcess(device);
    device->Release();
}
