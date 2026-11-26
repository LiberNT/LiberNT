/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the COM apartment that the input context gives a thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <ole2.h>
#include <imm.h>
#include <imm32_undoc.h>
#include <apitest.h>

static HANDLE g_hMtaStop;

#define ok_no_apartment() CheckApartment(__LINE__, FALSE, APTTYPE_STA, APTTYPEQUALIFIER_NONE)
#define ok_apartment(type) CheckApartment(__LINE__, TRUE, type, APTTYPEQUALIFIER_NONE)
#define ok_apartment_qualified(type, qualifier) CheckApartment(__LINE__, TRUE, type, qualifier)

static void CheckApartment(int line, BOOL fInitialized, APTTYPE Expected, APTTYPEQUALIFIER ExpectedQualifier)
{
    APTTYPE Type = (APTTYPE)-2;
    APTTYPEQUALIFIER Qualifier = (APTTYPEQUALIFIER)-1;
    HRESULT hr = CoGetApartmentType(&Type, &Qualifier);

    if (!fInitialized)
    {
        ok_(__FILE__, line)(hr == CO_E_NOTINITIALIZED, "CoGetApartmentType returned 0x%lx, type %d\n", hr, Type);
        return;
    }
    ok_(__FILE__, line)(hr == S_OK, "CoGetApartmentType returned 0x%lx\n", hr);
    ok_(__FILE__, line)(Type == Expected, "apartment type %d, expected %d\n", Type, Expected);
    ok_(__FILE__, line)(Qualifier == ExpectedQualifier, "qualifier %d, expected %d\n", Qualifier, ExpectedQualifier);
}

static HWND CreateHiddenWindow(void)
{
    HWND hwnd = CreateWindowExW(0, L"static", L"", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);

    ok(hwnd != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    return hwnd;
}

static HWND CreateFocusedWindow(void)
{
    HWND hwnd = CreateWindowExW(0, L"EDIT", L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 100, NULL, NULL,
                                GetModuleHandleW(NULL), NULL);

    ok(hwnd != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (hwnd)
    {
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        for (int i = 0; i < 10; i++)
        {
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
            Sleep(50);
        }
    }
    return hwnd;
}

/* The apartment comes with the active input context and ends with the last window; the application's own
   initialization nests in it */
static DWORD WINAPI ActivationThread(LPVOID pv)
{
    HWND hwnd;

    ok_no_apartment();
    hwnd = CreateHiddenWindow();
    ok_no_apartment();
    ok(ImmSetActiveContext(NULL, NULL, TRUE), "ImmSetActiveContext failed\n");
    ok_apartment(APTTYPE_MAINSTA);
    ok(CoInitialize(NULL) == S_OK, "CoInitialize did not return S_OK\n");
    ok_apartment(APTTYPE_MAINSTA);
    CoUninitialize();
    ok_apartment(APTTYPE_MAINSTA);
    DestroyWindow(hwnd);
    ok_no_apartment();
    return 0;
}

/* Without a window, the application's uninitialization also ends the apartment of the input context */
static DWORD WINAPI WindowlessThread(LPVOID pv)
{
    ok(ImmSetActiveContext(NULL, NULL, TRUE), "ImmSetActiveContext failed\n");
    ok_apartment(APTTYPE_MAINSTA);
    ok(CoInitialize(NULL) == S_OK, "CoInitialize did not return S_OK\n");
    CoUninitialize();
    ok_no_apartment();
    return 0;
}

/* An application choosing the multithreaded apartment replaces the input context's one for good */
static DWORD WINAPI MultithreadedThread(LPVOID pv)
{
    HWND hwnd = CreateHiddenWindow();
    HRESULT hr;

    ok(ImmSetActiveContext(NULL, NULL, TRUE), "ImmSetActiveContext failed\n");
    ok_apartment(APTTYPE_MAINSTA);
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx returned 0x%lx\n", hr);
    ok_apartment(APTTYPE_MTA);
    CoUninitialize();
    ok_no_apartment();
    ok(ImmSetActiveContext(NULL, NULL, TRUE), "ImmSetActiveContext failed\n");
    ok_no_apartment();
    DestroyWindow(hwnd);
    ok_no_apartment();
    return 0;
}

/* A focused window activates the input context; the multithreaded apartment still replaces its apartment */
static DWORD WINAPI FocusThread(LPVOID pv)
{
    BOOL fMtaElsewhere = (pv != NULL);
    HWND hwnd = CreateFocusedWindow();
    HRESULT hr;

    ok_apartment(APTTYPE_MAINSTA);
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx returned 0x%lx\n", hr);
    ok_apartment(APTTYPE_MTA);
    CoUninitialize();
    if (fMtaElsewhere)
        ok_apartment_qualified(APTTYPE_MTA, APTTYPEQUALIFIER_IMPLICIT_MTA);
    else
        ok_no_apartment();
    DestroyWindow(hwnd);
    return 0;
}

static DWORD WINAPI MtaThread(LPVOID pv)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    SetEvent((HANDLE)pv);
    WaitForSingleObject(g_hMtaStop, INFINITE);
    CoUninitialize();
    return 0;
}

static void RunThread(const char *Name, LPTHREAD_START_ROUTINE Routine, LPVOID pv)
{
    HANDLE hThread;

    winetest_push_context("%s", Name);
    hThread = CreateThread(NULL, 0, Routine, pv, 0, NULL);
    ok(hThread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (hThread)
    {
        ok(WaitForSingleObject(hThread, 30000) == WAIT_OBJECT_0, "the thread did not finish\n");
        CloseHandle(hThread);
    }
    winetest_pop_context();
}

START_TEST(coinit)
{
    HANDLE hReady, hMta;

    RunThread("activation", ActivationThread, NULL);
    RunThread("windowless", WindowlessThread, NULL);
    RunThread("multithreaded", MultithreadedThread, NULL);
    RunThread("focus", FocusThread, NULL);

    hReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_hMtaStop = CreateEventW(NULL, TRUE, FALSE, NULL);
    hMta = CreateThread(NULL, 0, MtaThread, hReady, 0, NULL);
    ok(hMta != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (hMta)
    {
        ok(WaitForSingleObject(hReady, 5000) == WAIT_OBJECT_0, "the MTA thread did not start\n");
        RunThread("focus, MTA elsewhere", FocusThread, hMta);
        SetEvent(g_hMtaStop);
        WaitForSingleObject(hMta, 5000);
        CloseHandle(hMta);
    }
    CloseHandle(g_hMtaStop);
    CloseHandle(hReady);
}
