/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Which deleted list box and combo box items the owner hears of through WM_DELETEITEM
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define MAX_DELETED 8

static ULONG_PTR DeletedData[MAX_DELETED];
static UINT DeletedItem[MAX_DELETED];
static UINT DeletedCount;

static LRESULT CALLBACK
OwnerProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_DELETEITEM)
    {
        const DELETEITEMSTRUCT *Item = (const DELETEITEMSTRUCT *)LParam;

        if (DeletedCount < MAX_DELETED)
        {
            DeletedItem[DeletedCount] = Item->itemID;
            DeletedData[DeletedCount] = Item->itemData;
        }
        DeletedCount++;
        return TRUE;
    }
    if (Message == WM_MEASUREITEM)
    {
        ((MEASUREITEMSTRUCT *)LParam)->itemHeight = 16;
        return TRUE;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static void
ExpectDeleted(PCSTR Name, PCSTR Step, UINT Count, UINT Item0, ULONG_PTR Data0, UINT Item1, ULONG_PTR Data1)
{
    ok(DeletedCount == Count, "%s %s: %u WM_DELETEITEM, expected %u\n", Name, Step, DeletedCount, Count);
    if (Count > 0 && DeletedCount > 0)
        ok(DeletedItem[0] == Item0 && DeletedData[0] == Data0, "%s %s: first item %u data %Ix, expected %u data %Ix\n",
           Name, Step, DeletedItem[0], DeletedData[0], Item0, Data0);
    if (Count > 1 && DeletedCount > 1)
        ok(DeletedItem[1] == Item1 && DeletedData[1] == Data1, "%s %s: second item %u data %Ix, expected %u data %Ix\n",
           Name, Step, DeletedItem[1], DeletedData[1], Item1, Data1);
    DeletedCount = 0;
}

static void
TestControl(HWND Owner, PCWSTR Class, DWORD Style, BOOL Combo, BOOL OwnerDraw, PCSTR Name)
{
    UINT Add = Combo ? CB_ADDSTRING : LB_ADDSTRING;
    UINT SetData = Combo ? CB_SETITEMDATA : LB_SETITEMDATA;
    UINT Delete = Combo ? CB_DELETESTRING : LB_DELETESTRING;
    UINT Reset = Combo ? CB_RESETCONTENT : LB_RESETCONTENT;
    HWND Control;

    Control = CreateWindowExW(0, Class, L"", WS_CHILD | WS_VISIBLE | Style, 0, 0, 200, 200, Owner, (HMENU)77, NULL, NULL);
    ok(Control != NULL, "%s: CreateWindowEx failed with %lu\n", Name, GetLastError());
    if (Control == NULL)
        return;

    SendMessageW(Control, Add, 0, (LPARAM)L"zero");
    SendMessageW(Control, Add, 0, (LPARAM)L"one");
    SendMessageW(Control, Add, 0, (LPARAM)L"two");
    SendMessageW(Control, SetData, 1, 0x1234);
    DeletedCount = 0;

    SendMessageW(Control, Delete, 0, 0);
    ExpectDeleted(Name, "delete of an item without data", 1, 0, 0, 0, 0);
    SendMessageW(Control, Delete, 0, 0);
    ExpectDeleted(Name, "delete of an item with data", 1, 0, 0x1234, 0, 0);

    SendMessageW(Control, Add, 0, (LPARAM)L"three");
    SendMessageW(Control, SetData, 1, 0x5678);
    DeletedCount = 0;
    SendMessageW(Control, Reset, 0, 0);
    if (OwnerDraw)
        ExpectDeleted(Name, "reset", 2, 1, 0x5678, 0, 0);
    else
        ExpectDeleted(Name, "reset", 0, 0, 0, 0, 0);

    SendMessageW(Control, Add, 0, (LPARAM)L"four");
    SendMessageW(Control, Add, 0, (LPARAM)L"five");
    SendMessageW(Control, SetData, 0, 0x9abc);
    DeletedCount = 0;
    DestroyWindow(Control);
    if (OwnerDraw)
        ExpectDeleted(Name, "destroy", 2, 1, 0, 0, 0x9abc);
    else
        ExpectDeleted(Name, "destroy", 0, 0, 0, 0, 0);
}

START_TEST(DeleteItemMessage)
{
    WNDCLASSW Class = { 0 };
    HWND Owner;

    Class.lpfnWndProc = OwnerProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"DeleteItemMessageOwner";
    ok(RegisterClassW(&Class) != 0, "RegisterClass failed with %lu\n", GetLastError());
    Owner = CreateWindowExW(0, Class.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 300, 300, NULL, NULL, NULL, NULL);
    ok(Owner != NULL, "CreateWindowEx failed with %lu\n", GetLastError());
    if (Owner == NULL)
        return;

    TestControl(Owner, L"LISTBOX", LBS_NOTIFY, FALSE, FALSE, "plain list box");
    TestControl(Owner, L"LISTBOX", LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, FALSE, TRUE, "owner-draw list box");
    TestControl(Owner, L"LISTBOX", LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS, FALSE, TRUE, "variable owner-draw list box");
    TestControl(Owner, L"COMBOBOX", CBS_DROPDOWNLIST, TRUE, FALSE, "plain combo box");
    TestControl(Owner, L"COMBOBOX", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, TRUE, TRUE,
                "owner-draw combo box");

    DestroyWindow(Owner);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
