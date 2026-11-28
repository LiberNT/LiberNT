/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests RevokeDragDrop on a window whose drop target another process registered
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <wine/test.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <objbase.h>
#include <ole2.h>
#include <stdio.h>

static LONG target_refs = 1;

static HRESULT WINAPI target_QueryInterface(IDropTarget *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget))
    {
        *out = iface;
        InterlockedIncrement(&target_refs);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI target_AddRef(IDropTarget *iface)
{
    return InterlockedIncrement(&target_refs);
}

static ULONG WINAPI target_Release(IDropTarget *iface)
{
    return InterlockedDecrement(&target_refs);
}

static HRESULT WINAPI target_DragEnter(IDropTarget *iface, IDataObject *data, DWORD keys, POINTL pt, DWORD *effect)
{
    return S_OK;
}

static HRESULT WINAPI target_DragOver(IDropTarget *iface, DWORD keys, POINTL pt, DWORD *effect)
{
    return S_OK;
}

static HRESULT WINAPI target_DragLeave(IDropTarget *iface)
{
    return S_OK;
}

static HRESULT WINAPI target_Drop(IDropTarget *iface, IDataObject *data, DWORD keys, POINTL pt, DWORD *effect)
{
    return S_OK;
}

static IDropTargetVtbl target_vtbl =
{
    target_QueryInterface, target_AddRef, target_Release,
    target_DragEnter, target_DragOver, target_DragLeave, target_Drop
};

static IDropTarget target = { &target_vtbl };

static void run_child(HWND hwnd)
{
    HRESULT hr;

    hr = OleInitialize(NULL);
    ok(hr == S_OK, "OleInitialize returned %#lx\n", hr);
    ok(IsWindow(hwnd), "window %p is gone\n", hwnd);

    hr = RevokeDragDrop(hwnd);
    ok(hr == RPC_E_WRONG_THREAD, "RevokeDragDrop on the other process's window returned %#lx\n", hr);
    hr = RevokeDragDrop(hwnd);
    ok(hr == DRAGDROP_E_NOTREGISTERED, "second RevokeDragDrop returned %#lx\n", hr);

    OleUninitialize();
}

START_TEST(revokeforeign)
{
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    char **argv, cmd[MAX_PATH * 2];
    HWND hwnd;
    HRESULT hr;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc > 3)
    {
        run_child((HWND)(ULONG_PTR)_strtoui64(argv[3], NULL, 16));
        return;
    }

    hr = OleInitialize(NULL);
    ok(hr == S_OK, "OleInitialize returned %#lx\n", hr);
    hwnd = CreateWindowA("STATIC", "revokeforeign", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    ok(hwnd != NULL, "CreateWindowA failed %lu\n", GetLastError());

    hr = RegisterDragDrop(hwnd, &target);
    ok(hr == S_OK, "RegisterDragDrop returned %#lx\n", hr);
    ok(target_refs == 2, "target holds %ld references after RegisterDragDrop\n", target_refs);

    sprintf(cmd, "\"%s\" revokeforeign %p", argv[0], (void *)hwnd);
    ok(CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi), "CreateProcessA failed %lu\n",
       GetLastError());
    winetest_wait_child_process(pi.hProcess);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    ok(target_refs == 2, "target holds %ld references after the other process revoked it\n", target_refs);
    hr = RevokeDragDrop(hwnd);
    ok(hr == DRAGDROP_E_NOTREGISTERED, "RevokeDragDrop by the owner returned %#lx\n", hr);
    ok(target_refs == 2, "target holds %ld references after the owner's RevokeDragDrop\n", target_refs);

    DestroyWindow(hwnd);
    OleUninitialize();
}
