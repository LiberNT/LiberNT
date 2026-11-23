/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Records copy out, copy in, rename and delete through the Portable Devices shell folders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <shlobj.h>
#include <shlwapi.h>

static void
ItemName(IShellFolder *Folder, PCUITEMID_CHILD Item, SHGDNF Flags, WCHAR *Text, UINT Chars)
{
    STRRET Ret;

    Text[0] = 0;
    if (SUCCEEDED(IShellFolder_GetDisplayNameOf(Folder, Item, Flags, &Ret)))
        StrRetToBufW(&Ret, Item, Text, Chars);
}

static PITEMID_CHILD
FindChild(IShellFolder *Folder, const WCHAR *Match, BOOL Contains)
{
    IEnumIDList *Enum;
    PITEMID_CHILD Item, Found = NULL;
    WCHAR Text[1024];
    ULONG Fetched;

    if (IShellFolder_EnumObjects(Folder, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &Enum) != S_OK)
        return NULL;
    while (!Found && IEnumIDList_Next(Enum, 1, &Item, &Fetched) == S_OK && Fetched)
    {
        ItemName(Folder, Item, Contains ? SHGDN_FORPARSING : SHGDN_INFOLDER, Text, ARRAYSIZE(Text));
        _wcslwr(Text);
        if (Contains ? wcsstr(Text, Match) != NULL : !wcsncmp(Text, Match, wcslen(Match)))
            Found = Item;
        else
            ILFree(Item);
    }
    IEnumIDList_Release(Enum);
    return Found;
}

static IShellFolder *
BindChild(IShellFolder *Folder, PCUITEMID_CHILD Item)
{
    IShellFolder *Child = NULL;

    if (FAILED(IShellFolder_BindToObject(Folder, Item, NULL, &IID_IShellFolder, (void **)&Child)))
        return NULL;
    return Child;
}

static IShellFolder *
OpenStorage(void)
{
    IShellFolder *Desktop, *Computer = NULL, *Device = NULL, *Storage = NULL;
    PIDLIST_RELATIVE Pidl;
    PITEMID_CHILD Item;
    IEnumIDList *Enum;
    ULONG Fetched;

    if (FAILED(SHGetDesktopFolder(&Desktop)))
        return NULL;
    if (SUCCEEDED(IShellFolder_ParseDisplayName(Desktop, NULL, NULL, (LPWSTR)L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",
                                                NULL, &Pidl, NULL)))
    {
        IShellFolder_BindToObject(Desktop, Pidl, NULL, &IID_IShellFolder, (void **)&Computer);
        ILFree(Pidl);
    }
    IShellFolder_Release(Desktop);
    if (!Computer)
        return NULL;
    Item = FindChild(Computer, L"#rich2#", TRUE);
    if (Item)
    {
        Device = BindChild(Computer, Item);
        ILFree(Item);
    }
    IShellFolder_Release(Computer);
    if (!Device)
        return NULL;
    if (IShellFolder_EnumObjects(Device, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &Enum) == S_OK)
    {
        if (IEnumIDList_Next(Enum, 1, &Item, &Fetched) == S_OK && Fetched)
        {
            Storage = BindChild(Device, Item);
            ILFree(Item);
        }
        IEnumIDList_Release(Enum);
    }
    IShellFolder_Release(Device);
    return Storage;
}

static void
DumpFormats(IDataObject *Data)
{
    IEnumFORMATETC *Enum;
    FORMATETC Format;
    STGMEDIUM Medium;
    WCHAR Name[128];
    char Hex[3 * 96 + 1];
    BYTE *Bytes;
    SIZE_T Size, Index;
    ULONG Fetched;

    if (FAILED(IDataObject_EnumFormatEtc(Data, DATADIR_GET, &Enum)))
    {
        trace("EnumFormatEtc failed\n");
        return;
    }
    while (IEnumFORMATETC_Next(Enum, 1, &Format, &Fetched) == S_OK && Fetched)
    {
        Name[0] = 0;
        if (!GetClipboardFormatNameW(Format.cfFormat, Name, ARRAYSIZE(Name)))
            wsprintfW(Name, L"#%u", Format.cfFormat);
        trace("  format \"%ls\" tymed 0x%lX lindex %ld\n", Name, Format.tymed, Format.lindex);
        if (wcsncmp(Name, L"WPD", 3) || !(Format.tymed & TYMED_HGLOBAL))
            continue;
        Format.tymed = TYMED_HGLOBAL;
        if (FAILED(IDataObject_GetData(Data, &Format, &Medium)))
        {
            trace("    GetData failed\n");
            continue;
        }
        Size = GlobalSize(Medium.hGlobal);
        Bytes = GlobalLock(Medium.hGlobal);
        Hex[0] = 0;
        for (Index = 0; Bytes && Index < Size && Index < 96; Index++)
            sprintf(Hex + 3 * Index, "%02X ", Bytes[Index]);
        trace("    size %Iu: %s\n", (size_t)Size, Hex);
        if (Bytes && Size >= sizeof(WCHAR))
            trace("    text \"%.*ls\"\n", (int)min(Size / sizeof(WCHAR), 200), (WCHAR *)Bytes);
        if (Bytes)
            GlobalUnlock(Medium.hGlobal);
        ReleaseStgMedium(&Medium);
    }
    IEnumFORMATETC_Release(Enum);
}

static void
DumpCopyOut(IShellFolder *Folder, PCUITEMID_CHILD Item, const char *Label)
{
    FORMATETC Format = { 0, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    FILEGROUPDESCRIPTORW *Group;
    IDataObject *Data;
    STGMEDIUM Medium;
    BYTE Buffer[64];
    ULONG Read, Index;
    HRESULT hr;

    hr = IShellFolder_GetUIObjectOf(Folder, NULL, 1, &Item, &IID_IDataObject, NULL, (void **)&Data);
    trace("%s: GetUIObjectOf(IDataObject) hr 0x%08lX\n", Label, hr);
    if (FAILED(hr))
        return;
    DumpFormats(Data);
    Format.cfFormat = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
    hr = IDataObject_GetData(Data, &Format, &Medium);
    trace("%s: GetData(FileGroupDescriptorW) hr 0x%08lX\n", Label, hr);
    if (SUCCEEDED(hr))
    {
        Group = GlobalLock(Medium.hGlobal);
        trace("%s: items %u\n", Label, Group ? Group->cItems : 0);
        for (Index = 0; Group && Index < Group->cItems && Index < 16; Index++)
        {
            trace("%s:   [%lu] flags 0x%08lX attributes 0x%08lX size %lu \"%ls\"\n", Label, Index, Group->fgd[Index].dwFlags,
                  Group->fgd[Index].dwFileAttributes, Group->fgd[Index].nFileSizeLow, Group->fgd[Index].cFileName);
        }
        if (Group)
            GlobalUnlock(Medium.hGlobal);
        ReleaseStgMedium(&Medium);
    }
    Format.cfFormat = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILECONTENTSW);
    Format.lindex = 0;
    Format.tymed = TYMED_ISTREAM;
    hr = IDataObject_GetData(Data, &Format, &Medium);
    trace("%s: GetData(FileContents 0) hr 0x%08lX tymed %lu\n", Label, hr, SUCCEEDED(hr) ? Medium.tymed : 0);
    if (SUCCEEDED(hr) && Medium.tymed == TYMED_ISTREAM)
    {
        Read = 0;
        hr = IStream_Read(Medium.pstm, Buffer, sizeof(Buffer), &Read);
        trace("%s: Read hr 0x%08lX bytes %lu \"%.*s\"\n", Label, hr, Read, (int)Read, (char *)Buffer);
    }
    if (SUCCEEDED(hr))
        ReleaseStgMedium(&Medium);
    IDataObject_Release(Data);
}

static void
PumpWait(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Msg;

    while (GetTickCount() - Start < Milliseconds)
    {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }
    }
}

static void
WaitChild(IShellFolder *Folder, const WCHAR *Match, BOOL Present, DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    PITEMID_CHILD Item;

    for (;;)
    {
        Item = FindChild(Folder, Match, FALSE);
        if (Item)
            ILFree(Item);
        if ((Item != NULL) == Present || GetTickCount() - Start >= Milliseconds)
            return;
        PumpWait(500);
    }
}

static void
DumpChild(IShellFolder *Folder, const WCHAR *Match, const char *Label)
{
    PITEMID_CHILD Item = FindChild(Folder, Match, FALSE);
    WCHAR Text[512];

    if (!Item)
    {
        trace("%s: \"%ls\" not found\n", Label, Match);
        return;
    }
    ItemName(Folder, Item, SHGDN_INFOLDER | SHGDN_FOREDITING, Text, ARRAYSIZE(Text));
    trace("%s: \"%ls\" found, editing name \"%ls\"\n", Label, Match, Text);
    ILFree(Item);
}

START_TEST(ShellOps)
{
    IShellFolder *Storage, *Desktop, *Parent;
    IDataObject *Upload = NULL;
    IDropTarget *Target;
    IContextMenu *Menu;
    CMINVOKECOMMANDINFO Invoke;
    PITEMID_CHILD Item, Renamed;
    PIDLIST_ABSOLUTE Absolute;
    PCUITEMID_CHILD Last;
    WCHAR Path[MAX_PATH], Text[512];
    POINTL Point = { 0, 0 };
    DWORD Effect, Written;
    HANDLE File;
    HRESULT hr;

    WpdTraceIoctlsStart();
    winetest_mute_threshold = 0x7fffffff;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    OleInitialize(NULL);
    Storage = OpenStorage();
    ok(Storage != NULL, "storage folder not found\n");
    if (!Storage)
    {
        OleUninitialize();
        CoUninitialize();
        return;
    }

    Item = FindChild(Storage, L"hello", FALSE);
    if (Item)
    {
        DumpCopyOut(Storage, Item, "file");
        ILFree(Item);
    }
    Item = FindChild(Storage, L"docs", FALSE);
    if (Item)
    {
        DumpCopyOut(Storage, Item, "folder");
        ILFree(Item);
    }

    Item = FindChild(Storage, L"hello", FALSE);
    Renamed = FindChild(Storage, L"docs", FALSE);
    if (Item && Renamed)
    {
        PCUITEMID_CHILD Pair[2] = { Item, Renamed };
        IDataObject *Data;

        hr = IShellFolder_GetUIObjectOf(Storage, NULL, 2, Pair, &IID_IDataObject, NULL, (void **)&Data);
        trace("mixed: GetUIObjectOf(IDataObject) hr 0x%08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            DumpFormats(Data);
            IDataObject_Release(Data);
        }
    }
    if (Item && Renamed)
    {
        PCUITEMID_CHILD Pair[2] = { Renamed, Item };
        IDataObject *Data;

        hr = IShellFolder_GetUIObjectOf(Storage, NULL, 2, Pair, &IID_IDataObject, NULL, (void **)&Data);
        trace("mixed folder first: GetUIObjectOf(IDataObject) hr 0x%08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            DumpFormats(Data);
            IDataObject_Release(Data);
        }
    }
    if (Item)
        ILFree(Item);
    Item = FindChild(Storage, L"music", FALSE);
    if (Item && Renamed)
    {
        PCUITEMID_CHILD Pair[2] = { Renamed, Item };
        IDataObject *Data;

        hr = IShellFolder_GetUIObjectOf(Storage, NULL, 2, Pair, &IID_IDataObject, NULL, (void **)&Data);
        trace("two folders: GetUIObjectOf(IDataObject) hr 0x%08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            DumpFormats(Data);
            IDataObject_Release(Data);
        }
    }
    if (Item)
        ILFree(Item);
    if (Renamed)
        ILFree(Renamed);

    Item = FindChild(Storage, L"notes", FALSE);
    if (Item)
    {
        Renamed = NULL;
        hr = IShellFolder_SetNameOf(Storage, NULL, Item, L"renamed", SHGDN_INFOLDER | SHGDN_FOREDITING, &Renamed);
        trace("rename: SetNameOf hr 0x%08lX\n", hr);
        if (Renamed)
        {
            ItemName(Storage, Renamed, SHGDN_INFOLDER | SHGDN_FOREDITING, Text, ARRAYSIZE(Text));
            trace("rename: new editing name \"%ls\"\n", Text);
            hr = IShellFolder_SetNameOf(Storage, NULL, Renamed, L"notes", SHGDN_INFOLDER | SHGDN_FOREDITING, NULL);
            trace("rename: SetNameOf back hr 0x%08lX\n", hr);
            ILFree(Renamed);
        }
        ILFree(Item);
    }
    PumpWait(2000);
    Item = FindChild(Storage, L"renamed", FALSE);
    if (Item)
    {
        hr = IShellFolder_SetNameOf(Storage, NULL, Item, L"notes", SHGDN_INFOLDER | SHGDN_FOREDITING, NULL);
        trace("rename: SetNameOf back with an enumerated item hr 0x%08lX\n", hr);
        ILFree(Item);
    }
    Item = FindChild(Storage, L"renamed", FALSE);
    if (Item)
    {
        hr = IShellFolder_SetNameOf(Storage, NULL, Item, L"another", SHGDN_INFOLDER | SHGDN_FOREDITING, NULL);
        trace("rename: SetNameOf to a new name hr 0x%08lX\n", hr);
        ILFree(Item);
    }
    Item = FindChild(Storage, L"hello", FALSE);
    if (Item)
    {
        hr = IShellFolder_SetNameOf(Storage, NULL, Item, L"greeting", SHGDN_INFOLDER | SHGDN_FOREDITING, NULL);
        trace("rename: SetNameOf of an untouched item hr 0x%08lX\n", hr);
        ILFree(Item);
    }
    PumpWait(2000);
    DumpChild(Storage, L"another", "rename");
    DumpChild(Storage, L"greeting", "rename");
    PumpWait(2000);
    DumpChild(Storage, L"renamed", "rename");
    DumpChild(Storage, L"notes", "rename");

    GetTempPathW(ARRAYSIZE(Path), Path);
    PathAppendW(Path, L"upload.txt");
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File != INVALID_HANDLE_VALUE)
    {
        WriteFile(File, "uploaded!", 9, &Written, NULL);
        CloseHandle(File);
    }
    if (SUCCEEDED(SHParseDisplayName(Path, NULL, &Absolute, 0, NULL)))
    {
        if (SUCCEEDED(SHBindToParent(Absolute, &IID_IShellFolder, (void **)&Parent, &Last)))
        {
            IShellFolder_GetUIObjectOf(Parent, NULL, 1, &Last, &IID_IDataObject, NULL, (void **)&Upload);
            IShellFolder_Release(Parent);
        }
        ILFree(Absolute);
    }
    hr = IShellFolder_CreateViewObject(Storage, NULL, &IID_IDropTarget, (void **)&Target);
    trace("upload: CreateViewObject(IDropTarget) hr 0x%08lX\n", hr);
    if (SUCCEEDED(hr) && Upload)
    {
        Effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
        hr = IDropTarget_DragEnter(Target, Upload, MK_LBUTTON, Point, &Effect);
        trace("upload: DragEnter hr 0x%08lX effect 0x%lX\n", hr, Effect);
        Effect = DROPEFFECT_COPY;
        hr = IDropTarget_Drop(Target, Upload, MK_LBUTTON, Point, &Effect);
        trace("upload: Drop hr 0x%08lX effect 0x%lX\n", hr, Effect);
        IDropTarget_Release(Target);
    }
    if (Upload)
        IDataObject_Release(Upload);
    WaitChild(Storage, L"upload", TRUE, 15000);
    DumpChild(Storage, L"upload", "upload");

    Item = FindChild(Storage, L"upload", FALSE);
    if (Item)
    {
        hr = IShellFolder_GetUIObjectOf(Storage, NULL, 1, (PCUITEMID_CHILD_ARRAY)&Item, &IID_IContextMenu, NULL, (void **)&Menu);
        trace("delete: GetUIObjectOf(IContextMenu) hr 0x%08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            HMENU Popup = CreatePopupMenu();

            IContextMenu_QueryContextMenu(Menu, Popup, 0, 1, 0x7FFF, CMF_NORMAL);
            ZeroMemory(&Invoke, sizeof(Invoke));
            Invoke.cbSize = sizeof(Invoke);
            Invoke.fMask = CMIC_MASK_FLAG_NO_UI;
            Invoke.lpVerb = "delete";
            Invoke.nShow = SW_HIDE;
            hr = IContextMenu_InvokeCommand(Menu, &Invoke);
            trace("delete: InvokeCommand hr 0x%08lX\n", hr);
            DestroyMenu(Popup);
            IContextMenu_Release(Menu);
        }
        ILFree(Item);
    }
    WaitChild(Storage, L"upload", FALSE, 10000);
    DumpChild(Storage, L"upload", "delete");
    DeleteFileW(Path);

    IShellFolder_Release(Storage);
    hr = SHGetDesktopFolder(&Desktop);
    if (SUCCEEDED(hr))
        IShellFolder_Release(Desktop);
    OleUninitialize();
    CoUninitialize();
}
