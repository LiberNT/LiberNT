/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the CREATESTRUCT that call-window-procedure hooks and the window procedure receive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _CS_SEEN
{
    BOOL Seen;
    ULONG_PTR Name;
    ULONG_PTR Class;
    BOOL NameMatches;
    BOOL ClassMatches;
} CS_SEEN;

enum { SEEN_CWP, SEEN_CWPRET, SEEN_WNDPROC, SEEN_COUNT };

static CS_SEEN Seen[SEEN_COUNT][2];
static LPCWSTR ExpectedName;
static LPCWSTR ExpectedClassName;
static ATOM ClassAtom;
static const WCHAR ClassName[] = L"CreateStructHookClass";

static void
Record(int Source, HWND hWnd, UINT Message, const CREATESTRUCTW *Cs)
{
    CS_SEEN *Entry;

    if ((Message != WM_NCCREATE && Message != WM_CREATE) ||
        GetClassLongPtrW(hWnd, GCW_ATOM) != ClassAtom)
        return;
    Entry = &Seen[Source][Message == WM_CREATE];
    Entry->Seen = TRUE;
    Entry->Name = (ULONG_PTR)Cs->lpszName;
    Entry->Class = (ULONG_PTR)Cs->lpszClass;
    Entry->NameMatches = ExpectedName && !IS_INTRESOURCE(Cs->lpszName) &&
                         !IsBadStringPtrW(Cs->lpszName, 64) && !wcscmp(Cs->lpszName, ExpectedName);
    Entry->ClassMatches = !IS_INTRESOURCE(Cs->lpszClass) &&
                          !IsBadStringPtrW(Cs->lpszClass, 64) && !wcscmp(Cs->lpszClass, ExpectedClassName);
}

static LRESULT CALLBACK
CwpProc(int Code, WPARAM wParam, LPARAM lParam)
{
    const CWPSTRUCT *Cwp = (const CWPSTRUCT *)lParam;

    if (Code == HC_ACTION)
        Record(SEEN_CWP, Cwp->hwnd, Cwp->message, (const CREATESTRUCTW *)Cwp->lParam);
    return CallNextHookEx(NULL, Code, wParam, lParam);
}

static LRESULT CALLBACK
CwpRetProc(int Code, WPARAM wParam, LPARAM lParam)
{
    const CWPRETSTRUCT *Cwpr = (const CWPRETSTRUCT *)lParam;

    if (Code == HC_ACTION)
        Record(SEEN_CWPRET, Cwpr->hwnd, Cwpr->message, (const CREATESTRUCTW *)Cwpr->lParam);
    return CallNextHookEx(NULL, Code, wParam, lParam);
}

static LRESULT CALLBACK
WndProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_NCCREATE || Message == WM_CREATE)
        Record(SEEN_WNDPROC, hWnd, Message, (const CREATESTRUCTW *)lParam);
    return DefWindowProcW(hWnd, Message, wParam, lParam);
}

static void
CreateAndCheck(LPCWSTR Class, LPCWSTR Name, int Line)
{
    static const char *Sources[SEEN_COUNT] = { "WH_CALLWNDPROC", "WH_CALLWNDPROCRET", "window procedure" };
    HHOOK Cwp, CwpRet;
    HWND hWnd;
    int Source, Index;

    ZeroMemory(Seen, sizeof(Seen));
    ExpectedName = Name;
    ExpectedClassName = ClassName;
    Cwp = SetWindowsHookExW(WH_CALLWNDPROC, CwpProc, NULL, GetCurrentThreadId());
    CwpRet = SetWindowsHookExW(WH_CALLWNDPROCRET, CwpRetProc, NULL, GetCurrentThreadId());
    ok_(__FILE__, Line)(Cwp != NULL && CwpRet != NULL, "SetWindowsHookExW failed: %lu\n", GetLastError());

    hWnd = CreateWindowExW(0, Class, Name, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    ok_(__FILE__, Line)(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());

    if (Cwp)
        UnhookWindowsHookEx(Cwp);
    if (CwpRet)
        UnhookWindowsHookEx(CwpRet);
    if (hWnd)
        DestroyWindow(hWnd);

    for (Source = 0; Source < SEEN_COUNT; Source++)
    {
        for (Index = 0; Index < 2; Index++)
        {
            const CS_SEEN *Entry = &Seen[Source][Index];
            const char *Message = Index ? "WM_CREATE" : "WM_NCCREATE";

            ok_(__FILE__, Line)(Entry->Seen, "%s did not see %s\n", Sources[Source], Message);
            if (!Entry->Seen)
                continue;
            if (Name)
                ok_(__FILE__, Line)(Entry->NameMatches, "%s %s: lpszName %p is not the title\n",
                                    Sources[Source], Message, (PVOID)Entry->Name);
            else
                ok_(__FILE__, Line)(Entry->Name == 0, "%s %s: lpszName is %p, expected NULL\n",
                                    Sources[Source], Message, (PVOID)Entry->Name);
            if (IS_INTRESOURCE(Class))
                ok_(__FILE__, Line)(Entry->Class == (ULONG_PTR)Class || Entry->ClassMatches,
                                    "%s %s: lpszClass is %p, expected the atom %p or the class name\n",
                                    Sources[Source], Message, (PVOID)Entry->Class, Class);
            else
                ok_(__FILE__, Line)(Entry->ClassMatches, "%s %s: lpszClass %p is not the class name\n",
                                    Sources[Source], Message, (PVOID)Entry->Class);
        }
    }
}

START_TEST(CreateStructHook)
{
    WNDCLASSW Wc = { 0 };

    Wc.lpfnWndProc = WndProc;
    Wc.hInstance = GetModuleHandleW(NULL);
    Wc.lpszClassName = ClassName;
    ClassAtom = RegisterClassW(&Wc);
    ok(ClassAtom != 0, "RegisterClassW failed: %lu\n", GetLastError());
    if (!ClassAtom)
        return;

    CreateAndCheck((LPCWSTR)MAKEINTATOM(ClassAtom), NULL, __LINE__);
    CreateAndCheck((LPCWSTR)MAKEINTATOM(ClassAtom), L"Window title", __LINE__);
    CreateAndCheck(ClassName, NULL, __LINE__);
    CreateAndCheck(ClassName, L"Window title", __LINE__);
    CreateAndCheck((LPCWSTR)MAKEINTATOM(ClassAtom), L"", __LINE__);
    CreateAndCheck(ClassName, L"", __LINE__);

    UnregisterClassW(ClassName, GetModuleHandleW(NULL));
}
