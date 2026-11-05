/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A thread blocked on a fence that never completes can still be terminated
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "d3d12_apitest.h"
#include <stdio.h>

#define FENCEKILL_READY_MS 30000
#define FENCEKILL_SETTLE_MS 500
#define FENCEKILL_EXIT_MS 5000
#define FENCEKILL_CHILD_MS 20000
#define FENCEKILL_EXIT_CODE 7

static DWORD WINAPI BlockedWait(void *parameter)
{
    ID3D12Fence *fence = static_cast<ID3D12Fence *>(parameter);

    return fence->SetEventOnCompletion(1, NULL) == S_OK ? 0 : 1;
}

static UINT RunWaitChild(ID3D12Device *device, HANDLE shared, HANDLE ready)
{
    ID3D12Fence *fence = NULL;
    HRESULT hr;

    if (!device) return 2;
    hr = device->OpenSharedHandle(shared, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence));
    if (FAILED(hr)) return 3;
    SetEvent(ready);
    hr = fence->SetEventOnCompletion(1, NULL);
    fence->Release();
    return hr == S_OK ? 4 : 5;
}

static UINT RunThreadChild(ID3D12Device *device)
{
    ID3D12Fence *fence = NULL;
    HANDLE thread;
    DWORD thread_code = 0;
    UINT step = 0;

    if (!device) return 2;
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence))))
        return 20;
    thread = CreateThread(NULL, 0, BlockedWait, fence, 0, NULL);
    if (!thread)
    {
        fence->Release();
        return 21;
    }
    Sleep(FENCEKILL_SETTLE_MS);
    if (WaitForSingleObject(thread, 0) != WAIT_TIMEOUT)
        step = 22;
    else if (!TerminateThread(thread, FENCEKILL_EXIT_CODE))
        step = 23;
    else if (WaitForSingleObject(thread, FENCEKILL_EXIT_MS) != WAIT_OBJECT_0)
        step = 24;
    else if (!GetExitCodeThread(thread, &thread_code) || thread_code != FENCEKILL_EXIT_CODE)
        step = 25;
    if (FAILED(fence->Signal(1)) && !step)
        step = 26;
    if (!step && fence->GetCompletedValue() != 1)
        step = 27;
    if (!step && fence->SetEventOnCompletion(1, NULL) != S_OK)
        step = 28;
    WaitForSingleObject(thread, FENCEKILL_EXIT_MS);
    CloseHandle(thread);
    fence->Release();
    return step;
}

static BOOL StartChild(const char *arguments, BOOL inherit, PROCESS_INFORMATION *info)
{
    STARTUPINFOA startup = {};
    char command[MAX_PATH + 96];
    char **argv;

    winetest_get_mainargs(&argv);
    snprintf(command, sizeof(command), "\"%s\" fencekill %s", argv[0], arguments);
    startup.cb = sizeof(startup);
    return CreateProcessA(NULL, command, NULL, NULL, inherit, 0, NULL, NULL, &startup, info);
}

static void TestTerminateProcess(ID3D12Device *device)
{
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), NULL, TRUE };
    PROCESS_INFORMATION info = {};
    ID3D12Fence *fence = NULL;
    HANDLE shared = NULL, ready = NULL, objects[2];
    char arguments[64];
    DWORD wait, start, exit_code = 0xdeadbeef;
    HRESULT hr;

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence));
    ok(hr == S_OK, "CreateFence(SHARED): %#lx\n", hr);
    if (!fence) return;
    hr = device->CreateSharedHandle(fence, &attributes, GENERIC_ALL, NULL, &shared);
    ok(hr == S_OK, "CreateSharedHandle(inheritable): %#lx\n", hr);
    ready = CreateEventA(&attributes, TRUE, FALSE, NULL);
    if (!shared || !ready)
    {
        skip("no inheritable fence or event for the child\n");
        goto done;
    }
    snprintf(arguments, sizeof(arguments), "wait %Ix %Ix",
             reinterpret_cast<ULONG_PTR>(shared), reinterpret_cast<ULONG_PTR>(ready));
    ok(StartChild(arguments, TRUE, &info), "CreateProcess failed: %lu\n", GetLastError());
    if (!info.hProcess) goto done;
    objects[0] = ready;
    objects[1] = info.hProcess;
    wait = WaitForMultipleObjects(2, objects, FALSE, FENCEKILL_READY_MS);
    ok(wait == WAIT_OBJECT_0, "child did not reach the fence wait: %lu\n", wait);
    if (wait == WAIT_OBJECT_0)
    {
        Sleep(FENCEKILL_SETTLE_MS);
        ok(WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT, "child left a wait on a fence that never completed\n");
        start = GetTickCount();
        ok(TerminateProcess(info.hProcess, FENCEKILL_EXIT_CODE), "TerminateProcess failed: %lu\n", GetLastError());
        wait = WaitForSingleObject(info.hProcess, FENCEKILL_EXIT_MS);
        trace("child blocked on a fence exited %lu ms after TerminateProcess\n", GetTickCount() - start);
        ok(wait == WAIT_OBJECT_0, "child blocked on a fence did not exit after TerminateProcess: %lu\n", wait);
    }
    if (WaitForSingleObject(info.hProcess, 0) != WAIT_OBJECT_0)
    {
        fence->Signal(1);
        if (WaitForSingleObject(info.hProcess, FENCEKILL_EXIT_MS) != WAIT_OBJECT_0)
            TerminateProcess(info.hProcess, 1);
    }
    GetExitCodeProcess(info.hProcess, &exit_code);
    ok(exit_code == FENCEKILL_EXIT_CODE, "child exit code %lu\n", exit_code);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
done:
    if (ready) CloseHandle(ready);
    if (shared) CloseHandle(shared);
    fence->Release();
}

static void TestTerminateThread(void)
{
    PROCESS_INFORMATION info = {};
    DWORD wait, exit_code = 0xdeadbeef;

    ok(StartChild("thread", FALSE, &info), "CreateProcess failed: %lu\n", GetLastError());
    if (!info.hProcess) return;
    wait = WaitForSingleObject(info.hProcess, FENCEKILL_CHILD_MS);
    ok(wait == WAIT_OBJECT_0, "child did not finish: %lu\n", wait);
    if (wait != WAIT_OBJECT_0)
        TerminateProcess(info.hProcess, 1);
    GetExitCodeProcess(info.hProcess, &exit_code);
    ok(exit_code == 0, "terminating a thread blocked on a fence failed at step %lu\n", exit_code);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
}

START_TEST(fencekill)
{
    ID3D12Device *device;
    char **argv;
    int argc = winetest_get_mainargs(&argv);

    if (argc >= 5 && !strcmp(argv[2], "wait"))
    {
        device = d3d12_test_create_device();
        ExitProcess(RunWaitChild(device,
                                 reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(_strtoui64(argv[3], NULL, 16))),
                                 reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(_strtoui64(argv[4], NULL, 16)))));
    }
    if (argc >= 3 && !strcmp(argv[2], "thread"))
    {
        device = d3d12_test_create_device();
        ExitProcess(RunThreadChild(device));
    }
    device = d3d12_test_create_device();
    if (!device) return;
    TestTerminateProcess(device);
    TestTerminateThread();
    device->Release();
}
