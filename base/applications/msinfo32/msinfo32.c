/*
 * PROJECT:     LiberNT System Information
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System summary window
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <strsafe.h>

#include "resource.h"

static const WCHAR g_ClassName[] = L"MSInfo32Main";
static const WCHAR g_CurrentVersionKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
static const WCHAR g_BiosKey[] = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
static const WCHAR g_ProcessorKey[] = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";

static HINSTANCE g_Instance;
static HWND g_List;

static VOID
AddItem(
    _In_ UINT NameId,
    _In_ PCWSTR Value)
{
    WCHAR Name[128];
    LVITEMW Item;

    if (!*Value || !LoadStringW(g_Instance, NameId, Name, ARRAYSIZE(Name)))
        return;

    ZeroMemory(&Item, sizeof(Item));
    Item.mask = LVIF_TEXT;
    Item.iItem = ListView_GetItemCount(g_List);
    Item.pszText = Name;
    Item.iItem = ListView_InsertItem(g_List, &Item);
    if (Item.iItem >= 0)
        ListView_SetItemText(g_List, Item.iItem, 1, (PWSTR)Value);
}

static VOID
AddString(
    _In_ UINT NameId,
    _In_ UINT ValueId)
{
    WCHAR Value[128];

    if (LoadStringW(g_Instance, ValueId, Value, ARRAYSIZE(Value)))
        AddItem(NameId, Value);
}

static VOID
AddSize(
    _In_ UINT NameId,
    _In_ ULONGLONG Bytes)
{
    WCHAR Value[64];

    if (StrFormatByteSizeW(Bytes, Value, ARRAYSIZE(Value)))
        AddItem(NameId, Value);
}

static BOOL
ReadMachineString(
    _In_ PCWSTR Key,
    _In_ PCWSTR Name,
    _Out_writes_(Count) PWSTR Buffer,
    _In_ DWORD Count)
{
    DWORD Size = Count * sizeof(WCHAR);

    Buffer[0] = UNICODE_NULL;
    return RegGetValueW(HKEY_LOCAL_MACHINE, Key, Name, RRF_RT_REG_SZ, NULL, Buffer, &Size) == ERROR_SUCCESS;
}

static VOID
FillSummary(VOID)
{
    WCHAR Text[512], Part[256], Other[128], Format[128];
    OSVERSIONINFOW Version;
    SYSTEM_INFO Info;
    MEMORYSTATUSEX Memory;
    TIME_ZONE_INFORMATION Zone;
    DWORD Size, ZoneId;

    SendMessageW(g_List, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_List);

    ReadMachineString(g_CurrentVersionKey, L"ProductName", Text, ARRAYSIZE(Text));
    AddItem(IDS_OS_NAME, Text);

    ZeroMemory(&Version, sizeof(Version));
    Version.dwOSVersionInfoSize = sizeof(Version);
    if (GetVersionExW(&Version) &&
        LoadStringW(g_Instance, IDS_VERSION_FORMAT, Format, ARRAYSIZE(Format)) &&
        SUCCEEDED(StringCchPrintfW(Text, ARRAYSIZE(Text), Format, Version.dwMajorVersion,
                                   Version.dwMinorVersion, Version.dwBuildNumber, Version.dwBuildNumber)))
    {
        AddItem(IDS_VERSION, Text);
    }

    Size = ARRAYSIZE(Text);
    if (GetComputerNameW(Text, &Size))
        AddItem(IDS_SYSTEM_NAME, Text);

    ReadMachineString(g_BiosKey, L"SystemManufacturer", Text, ARRAYSIZE(Text));
    AddItem(IDS_MANUFACTURER, Text);
    ReadMachineString(g_BiosKey, L"SystemProductName", Text, ARRAYSIZE(Text));
    AddItem(IDS_MODEL, Text);

    GetNativeSystemInfo(&Info);
    if (Info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
        AddString(IDS_SYSTEM_TYPE, IDS_TYPE_X64);
    else if (Info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
        AddString(IDS_SYSTEM_TYPE, IDS_TYPE_ARM64);
    else if (Info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
        AddString(IDS_SYSTEM_TYPE, IDS_TYPE_X86);

    if (ReadMachineString(g_ProcessorKey, L"ProcessorNameString", Part, ARRAYSIZE(Part)) &&
        LoadStringW(g_Instance, IDS_PROCESSOR_FORMAT, Format, ARRAYSIZE(Format)) &&
        SUCCEEDED(StringCchPrintfW(Text, ARRAYSIZE(Text), Format, Part, Info.dwNumberOfProcessors)))
    {
        AddItem(IDS_PROCESSOR, Text);
    }

    ReadMachineString(g_BiosKey, L"BIOSVendor", Text, ARRAYSIZE(Text));
    if (ReadMachineString(g_BiosKey, L"BIOSVersion", Part, ARRAYSIZE(Part)))
    {
        if (Text[0]) StringCchCatW(Text, ARRAYSIZE(Text), L" ");
        StringCchCatW(Text, ARRAYSIZE(Text), Part);
    }
    if (ReadMachineString(g_BiosKey, L"BIOSReleaseDate", Part, ARRAYSIZE(Part)))
    {
        if (Text[0]) StringCchCatW(Text, ARRAYSIZE(Text), L", ");
        StringCchCatW(Text, ARRAYSIZE(Text), Part);
    }
    AddItem(IDS_BIOS, Text);

    if (GetWindowsDirectoryW(Text, ARRAYSIZE(Text)))
        AddItem(IDS_WINDOWS_DIRECTORY, Text);
    if (GetSystemDirectoryW(Text, ARRAYSIZE(Text)))
        AddItem(IDS_SYSTEM_DIRECTORY, Text);

    if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SCOUNTRY, Text, ARRAYSIZE(Text)))
        AddItem(IDS_LOCALE, Text);

    Size = ARRAYSIZE(Part);
    if (GetComputerNameW(Part, &Size))
    {
        Size = ARRAYSIZE(Other);
        if (GetUserNameW(Other, &Size) &&
            SUCCEEDED(StringCchPrintfW(Text, ARRAYSIZE(Text), L"%s\\%s", Part, Other)))
        {
            AddItem(IDS_USER_NAME, Text);
        }
    }

    ZoneId = GetTimeZoneInformation(&Zone);
    if (ZoneId != TIME_ZONE_ID_INVALID)
        AddItem(IDS_TIME_ZONE, ZoneId == TIME_ZONE_ID_DAYLIGHT ? Zone.DaylightName : Zone.StandardName);

    ZeroMemory(&Memory, sizeof(Memory));
    Memory.dwLength = sizeof(Memory);
    if (GlobalMemoryStatusEx(&Memory))
    {
        AddSize(IDS_TOTAL_PHYSICAL, Memory.ullTotalPhys);
        AddSize(IDS_AVAILABLE_PHYSICAL, Memory.ullAvailPhys);
        AddSize(IDS_TOTAL_VIRTUAL, Memory.ullTotalPageFile);
        AddSize(IDS_AVAILABLE_VIRTUAL, Memory.ullAvailPageFile);
        if (Memory.ullTotalPageFile > Memory.ullTotalPhys)
            AddSize(IDS_PAGE_FILE, Memory.ullTotalPageFile - Memory.ullTotalPhys);
    }

    SendMessageW(g_List, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_List, NULL, TRUE);
}

static VOID
AddColumn(
    _In_ INT Index,
    _In_ UINT NameId,
    _In_ INT Width)
{
    WCHAR Name[64];
    LVCOLUMNW Column;

    if (!LoadStringW(g_Instance, NameId, Name, ARRAYSIZE(Name)))
        Name[0] = UNICODE_NULL;

    ZeroMemory(&Column, sizeof(Column));
    Column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    Column.pszText = Name;
    Column.cx = Width;
    Column.iSubItem = Index;
    ListView_InsertColumn(g_List, Index, &Column);
}

static LRESULT CALLBACK
MainWndProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    WCHAR Title[64];
    RECT Rect;

    switch (Message)
    {
        case WM_CREATE:
            g_List = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
                                     WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER,
                                     0, 0, 0, 0, Window, NULL, g_Instance, NULL);
            if (!g_List)
                return -1;
            ListView_SetExtendedListViewStyle(g_List, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            AddColumn(0, IDS_COLUMN_ITEM, 220);
            AddColumn(1, IDS_COLUMN_VALUE, 420);
            FillSummary();
            return 0;

        case WM_SIZE:
            GetClientRect(Window, &Rect);
            MoveWindow(g_List, 0, 0, Rect.right, Rect.bottom, TRUE);
            return 0;

        case WM_SETFOCUS:
            SetFocus(g_List);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDM_EXIT:
                    DestroyWindow(Window);
                    return 0;

                case IDM_REFRESH:
                    FillSummary();
                    return 0;

                case IDM_ABOUT:
                    if (LoadStringW(g_Instance, IDS_TITLE, Title, ARRAYSIZE(Title)))
                        ShellAboutW(Window, Title, NULL, LoadIconW(NULL, IDI_APPLICATION));
                    return 0;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(Window, Message, wParam, lParam);
}

int WINAPI
wWinMain(
    _In_ HINSTANCE Instance,
    _In_opt_ HINSTANCE PreviousInstance,
    _In_ PWSTR CommandLine,
    _In_ int Show)
{
    INITCOMMONCONTROLSEX Controls;
    WNDCLASSEXW Class;
    WCHAR Title[64];
    HACCEL Accelerators;
    HWND Window;
    MSG Message;

    UNREFERENCED_PARAMETER(PreviousInstance);
    UNREFERENCED_PARAMETER(CommandLine);

    g_Instance = Instance;

    Controls.dwSize = sizeof(Controls);
    Controls.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&Controls);

    ZeroMemory(&Class, sizeof(Class));
    Class.cbSize = sizeof(Class);
    Class.lpfnWndProc = MainWndProc;
    Class.hInstance = Instance;
    Class.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    Class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    Class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    Class.lpszMenuName = MAKEINTRESOURCEW(IDM_MAIN);
    Class.lpszClassName = g_ClassName;
    if (!RegisterClassExW(&Class))
        return 1;

    if (!LoadStringW(Instance, IDS_TITLE, Title, ARRAYSIZE(Title)))
        Title[0] = UNICODE_NULL;

    Window = CreateWindowExW(0, g_ClassName, Title, WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 720, 480,
                             NULL, NULL, Instance, NULL);
    if (!Window)
        return 1;

    ShowWindow(Window, Show);
    UpdateWindow(Window);

    Accelerators = LoadAcceleratorsW(Instance, MAKEINTRESOURCEW(IDA_MAIN));
    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        if (!TranslateAcceleratorW(Window, Accelerators, &Message))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
    }

    return (int)Message.wParam;
}
