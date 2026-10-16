/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests shell item property stores and property handlers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shellutils.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <stdio.h>
#include <strsafe.h>

static WCHAR g_Dir[MAX_PATH];
static WCHAR g_Txt[MAX_PATH];
static WCHAR g_Bmp[MAX_PATH];
static WCHAR g_Png[MAX_PATH];

static const BYTE g_PngData[] =
{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x30, 0x08, 0x02, 0x00, 0x00, 0x00, 0x2e, 0x29, 0xeb,
    0x48, 0x00, 0x00, 0x00, 0x43, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0xed, 0xcf, 0x41, 0x09, 0x00,
    0x00, 0x08, 0x04, 0xb0, 0x8b, 0x62, 0x54, 0xa3, 0x19, 0xcd, 0x0a, 0x7e, 0x85, 0xc1, 0x0a, 0x2c,
    0xd5, 0xf3, 0x5a, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0xae, 0x16, 0x22, 0xfa, 0x80, 0xf1, 0x94, 0x76, 0xbc, 0xf4,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static BOOL
WriteBytes(LPCWSTR Path, const void *Data, DWORD Size)
{
    HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD Written = 0;
    BOOL Ok;

    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ok = WriteFile(File, Data, Size, &Written, NULL) && Written == Size;
    CloseHandle(File);
    return Ok;
}

static BOOL
WriteBmp(LPCWSTR Path, LONG Width, LONG Height)
{
    LONG Stride = (Width * 3 + 3) & ~3;
    DWORD Size = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + Stride * Height;
    BYTE *Data = (BYTE *)calloc(1, Size);
    BITMAPFILEHEADER *File = (BITMAPFILEHEADER *)Data;
    BITMAPINFOHEADER *Info = (BITMAPINFOHEADER *)(File + 1);
    BOOL Ok;

    if (!Data)
        return FALSE;
    File->bfType = 0x4d42;
    File->bfSize = Size;
    File->bfOffBits = sizeof(*File) + sizeof(*Info);
    Info->biSize = sizeof(*Info);
    Info->biWidth = Width;
    Info->biHeight = Height;
    Info->biPlanes = 1;
    Info->biBitCount = 24;
    Ok = WriteBytes(Path, Data, Size);
    free(Data);
    return Ok;
}

static const char *
Esc(LPCWSTR Text)
{
    static char Buffers[4][1024];
    static int Next;
    char *Out = Buffers[Next++ & 3];
    size_t Used = 0;

    if (!Text)
        return "(null)";
    for (; *Text && Used + 8 < sizeof(Buffers[0]); Text++)
    {
        if (*Text >= 0x20 && *Text < 0x7f)
            Out[Used++] = (char)*Text;
        else
            Used += sprintf(Out + Used, "\\u%04x", *Text);
    }
    Out[Used] = 0;
    return Out;
}

static void
DumpStore(IPropertyStore *Store, LPCWSTR What)
{
    DWORD Count = 0;

    if (FAILED(Store->GetCount(&Count)))
        return;
    for (DWORD i = 0; i < Count; i++)
    {
        PROPERTYKEY Key;
        PROPVARIANT Value;
        LPWSTR Name = NULL, Text = NULL;

        if (FAILED(Store->GetAt(i, &Key)))
            continue;
        PropVariantInit(&Value);
        Store->GetValue(Key, &Value);
        PSGetNameFromPropertyKey(Key, &Name);
        PropVariantToStringAlloc(Value, &Text);
        printf("%s prop %s vt %u = %s\n", Esc(What), Name ? Esc(Name) : "?", Value.vt, Esc(Text ? Text : L""));
        CoTaskMemFree(Name);
        CoTaskMemFree(Text);
        PropVariantClear(&Value);
    }
}

static void
CheckString(IShellItem2 *Item, REFPROPERTYKEY Key, LPCWSTR Expected, LPCSTR What)
{
    LPWSTR Text = NULL;

    ok_hr(Item->GetString(Key, &Text), S_OK);
    ok(Text && !_wcsicmp(Text, Expected), "%s is %s\n", What, Esc(Text));
    CoTaskMemFree(Text);
}

static void
TraceStore(IShellItem2 *Item, GETPROPERTYSTOREFLAGS Flags, LPCWSTR What)
{
    CComPtr<IPropertyStore> Store;
    DWORD Count = 0;
    HRESULT hr;

    hr = Item->GetPropertyStore(Flags, IID_PPV_ARG(IPropertyStore, &Store));
    if (SUCCEEDED(hr))
        Store->GetCount(&Count);
    trace("%s store flags %#x: hr %#lx count %lu\n", Esc(What), Flags, hr, Count);
}

static void
CheckCommon(IShellItem2 *Item, LPCWSTR Path, BOOL Folder)
{
    WCHAR Parent[MAX_PATH], Owner[256], Computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD Length = _countof(Computer);
    PROPVARIANT Value;
    ULONG Flags = 0;
    BOOL IsFolder = !Folder;

    StringCchCopyW(Parent, _countof(Parent), Path);
    PathRemoveFileSpecW(Parent);
    CheckString(Item, PKEY_ItemFolderPathDisplay, Parent, "folder path");
    CheckString(Item, PKEY_ParsingPath, Path, "parsing path");
    CheckString(Item, PKEY_ItemFolderNameDisplay, PathFindFileNameW(Parent), "folder name");
    GetComputerNameW(Computer, &Length);
    CheckString(Item, PKEY_ComputerName, Computer, "computer name");
    {
        LPWSTR Text = NULL;
        ok_hr(Item->GetString(PKEY_FileOwner, &Text), S_OK);
        ok(Text && wcschr(Text, L'\\'), "owner %s\n", Esc(Text));
        StringCchCopyW(Owner, _countof(Owner), Text ? Text : L"");
        trace("owner %s\n", Esc(Owner));
        CoTaskMemFree(Text);
    }
    PROPERTYKEY IsFolderKey;
    ok_hr(PSGetPropertyKeyFromName(L"System.IsFolder", &IsFolderKey), S_OK);
    ok_hr(Item->GetBool(IsFolderKey, &IsFolder), S_OK);
    ok_int(IsFolder, Folder);
    ok_hr(Item->GetUInt32(PKEY_SFGAOFlags, &Flags), S_OK);
    ok((Flags & SFGAO_FILESYSTEM) != 0, "SFGAO %#lx\n", Flags);
    ok(!!(Flags & SFGAO_FOLDER) == !!Folder, "SFGAO %#lx\n", Flags);
    PropVariantInit(&Value);
    ok_hr(Item->GetProperty(PKEY_VolumeId, &Value), S_OK);
    ok(Value.vt == VT_CLSID, "volume id vt %u\n", Value.vt);
    PropVariantClear(&Value);
    PropVariantInit(&Value);
    ok_hr(Item->GetProperty(PKEY_Kind, &Value), S_OK);
    ok(Value.vt == (VT_VECTOR | VT_LPWSTR) && Value.calpwstr.cElems == 1, "kind vt %u\n", Value.vt);
    PropVariantClear(&Value);
}

static void
TestTextFile(void)
{
    CComPtr<IShellItem2> Item;
    CComPtr<IPropertyStore> Store;
    WIN32_FILE_ATTRIBUTE_DATA Attributes;
    ULONGLONG Size = 0;
    FILETIME Modified;
    LPWSTR Text = NULL;
    DWORD Count = 0;
    HRESULT hr;

    hr = SHCreateItemFromParsingName(g_Txt, NULL, IID_PPV_ARG(IShellItem2, &Item));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    ok_hr(Item->GetUInt64(PKEY_Size, &Size), S_OK);
    ok(Size == 1234, "size %I64u\n", Size);

    GetFileAttributesExW(g_Txt, GetFileExInfoStandard, &Attributes);
    ok_hr(Item->GetFileTime(PKEY_DateModified, &Modified), S_OK);
    ok(CompareFileTime(&Modified, &Attributes.ftLastWriteTime) == 0, "modified time differs\n");

    ok_hr(Item->GetString(PKEY_FileName, &Text), S_OK);
    ok(Text && !wcscmp(Text, L"note.txt"), "file name %ls\n", Text);
    CoTaskMemFree(Text);
    Text = NULL;

    ok_hr(Item->GetString(PKEY_ItemNameDisplay, &Text), S_OK);
    ok(Text && !wcsncmp(Text, L"note", 4), "display name %ls\n", Text);
    trace("item name display %s\n", Esc(Text));
    CoTaskMemFree(Text);
    Text = NULL;

    ok_hr(Item->GetString(PKEY_ItemPathDisplay, &Text), S_OK);
    ok(Text && !_wcsicmp(Text, g_Txt), "path display %ls\n", Text);
    CoTaskMemFree(Text);
    Text = NULL;

    ok_hr(Item->GetString(PKEY_ItemTypeText, &Text), S_OK);
    ok(Text && *Text, "empty type text\n");
    trace("text type %s\n", Esc(Text));
    CoTaskMemFree(Text);

    CheckCommon(Item, g_Txt, FALSE);
    CheckString(Item, PKEY_ItemType, L".txt", "item type");
    CheckString(Item, PKEY_FileExtension, L".txt", "extension");
    CheckString(Item, PKEY_ContentType, L"text/plain", "content type");
    CheckString(Item, PKEY_ItemNameDisplayWithoutExtension, L"note", "name without extension");
    CheckString(Item, PKEY_KindText, L"Document", "kind text");
    {
        int Perceived = 0;
        ok_hr(Item->GetInt32(PKEY_PerceivedType, &Perceived), S_OK);
        ok_int(Perceived, PERCEIVED_TYPE_TEXT);
    }
    {
        CLSID Handler;
        hr = PSLookupPropertyHandlerCLSID(g_Txt, &Handler);
        trace("text property handler: %#lx\n", hr);
    }
    TraceStore(Item, GPS_HANDLERPROPERTIESONLY, L"txt");
    TraceStore(Item, GPS_FASTPROPERTIESONLY, L"txt");

    hr = Item->GetPropertyStore(GPS_DEFAULT, IID_PPV_ARG(IPropertyStore, &Store));
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        ok_hr(Store->GetCount(&Count), S_OK);
        ok(Count >= 5, "only %lu properties\n", Count);
        trace("default store holds %lu properties\n", Count);
        DumpStore(Store, L"txt");
    }
    Store.Release();
    hr = Item->GetPropertyStore(GPS_READWRITE, IID_PPV_ARG(IPropertyStore, &Store));
    trace("read-write store on text: %#lx\n", hr);
}

static void
TestImageFile(void)
{
    CComPtr<IShellItem2> Item;
    CComPtr<IPropertyDescriptionList> List;
    LPWSTR Text = NULL;
    ULONG Value = 0;
    UINT Count = 0;
    HRESULT hr;

    hr = SHCreateItemFromParsingName(g_Bmp, NULL, IID_PPV_ARG(IShellItem2, &Item));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    ok_hr(Item->GetUInt32(PKEY_Image_HorizontalSize, &Value), S_OK);
    ok_long(Value, 64);
    Value = 0;
    ok_hr(Item->GetUInt32(PKEY_Image_VerticalSize, &Value), S_OK);
    ok_long(Value, 48);
    {
        CLSID Handler = CLSID_NULL;
        WCHAR Text[40];
        ok_hr(PSLookupPropertyHandlerCLSID(g_Bmp, &Handler), S_OK);
        StringFromGUID2(Handler, Text, _countof(Text));
        ok(!_wcsicmp(Text, L"{a38b883c-1682-497e-97b0-0a3a9e801682}"), "bmp handler %ls\n", Text);
    }
    CheckCommon(Item, g_Bmp, FALSE);
    CheckString(Item, PKEY_KindText, L"Picture", "kind text");
    CheckString(Item, PKEY_MIMEType, L"image/bmp", "mime type");
    ok_hr(Item->GetUInt32(PKEY_Image_BitDepth, &Value), S_OK);
    ok_long(Value, 24);
    TraceStore(Item, GPS_HANDLERPROPERTIESONLY, L"bmp");
    TraceStore(Item, GPS_FASTPROPERTIESONLY, L"bmp");
    TraceStore(Item, GPS_READWRITE, L"bmp");
    ok_hr(Item->GetString(PKEY_Image_Dimensions, &Text), S_OK);
    ok(Text && wcsstr(Text, L"64") && wcsstr(Text, L"48"), "dimensions %ls\n", Text);
    trace("dimensions %s\n", Esc(Text));
    CoTaskMemFree(Text);

    {
        CComPtr<IPropertyStore> Store;
        if (SUCCEEDED(Item->GetPropertyStore(GPS_DEFAULT, IID_PPV_ARG(IPropertyStore, &Store))))
            DumpStore(Store, L"bmp");
    }
    hr = Item->GetPropertyDescriptionList(PKEY_PropList_PreviewDetails, IID_PPV_ARG(IPropertyDescriptionList, &List));
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        ok_hr(List->GetCount(&Count), S_OK);
        ok(Count > 0, "empty preview details\n");
        trace("bmp preview details %u\n", Count);
        for (UINT i = 0; i < Count; i++)
        {
            CComPtr<IPropertyDescription> Desc;
            LPWSTR Name = NULL;

            if (SUCCEEDED(List->GetAt(i, IID_PPV_ARG(IPropertyDescription, &Desc))) &&
                SUCCEEDED(Desc->GetCanonicalName(&Name)))
            {
                trace("bmp preview detail %u %s\n", i, Esc(Name));
                CoTaskMemFree(Name);
            }
        }
    }
}

static void
TestPngFile(void)
{
    CComPtr<IShellItem2> Item;
    CComPtr<IPropertyStore> Store;
    ULONG Value = 0;
    HRESULT hr;

    hr = SHCreateItemFromParsingName(g_Png, NULL, IID_PPV_ARG(IShellItem2, &Item));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    ok_hr(Item->GetUInt32(PKEY_Image_HorizontalSize, &Value), S_OK);
    ok_long(Value, 64);
    ok_hr(Item->GetUInt32(PKEY_Image_BitDepth, &Value), S_OK);
    ok_long(Value, 24);
    CheckString(Item, PKEY_MIMEType, L"image/png", "png mime type");
    hr = Item->GetPropertyStore(GPS_HANDLERPROPERTIESONLY, IID_PPV_ARG(IPropertyStore, &Store));
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        DumpStore(Store, L"png handler");
}

static void
TestFolder(void)
{
    CComPtr<IShellItem2> Item;
    ULONGLONG Size = 0;
    LPWSTR Text = NULL;
    HRESULT hr;

    hr = SHCreateItemFromParsingName(g_Dir, NULL, IID_PPV_ARG(IShellItem2, &Item));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    hr = Item->GetUInt64(PKEY_Size, &Size);
    ok(FAILED(hr), "folder has a size: %#lx\n", hr);
    trace("folder size: %#lx\n", hr);
    ok_hr(Item->GetString(PKEY_ItemTypeText, &Text), S_OK);
    ok(Text && *Text, "empty folder type text\n");
    trace("folder type %s\n", Esc(Text));
    CoTaskMemFree(Text);
    CheckCommon(Item, g_Dir, TRUE);
    CheckString(Item, PKEY_ItemType, L"Directory", "folder item type");
    CheckString(Item, PKEY_KindText, L"Folder", "folder kind text");
    CheckString(Item, PKEY_ItemTypeText, L"File folder", "folder type text");
    {
        CComPtr<IPropertyStore> Store;
        if (SUCCEEDED(Item->GetPropertyStore(GPS_DEFAULT, IID_PPV_ARG(IPropertyStore, &Store))))
            DumpStore(Store, L"folder");
    }
}

START_TEST(ShellItemProperties)
{
    WCHAR Temp[MAX_PATH];
    char Content[1234];

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetTempPathW(_countof(Temp), Temp);
    wsprintfW(g_Dir, L"%spropstest%lu", Temp, GetCurrentProcessId());
    CreateDirectoryW(g_Dir, NULL);
    wsprintfW(g_Txt, L"%s\\note.txt", g_Dir);
    wsprintfW(g_Bmp, L"%s\\image.bmp", g_Dir);
    wsprintfW(g_Png, L"%s\\image.png", g_Dir);
    memset(Content, 'a', sizeof(Content));
    if (!WriteBytes(g_Txt, Content, sizeof(Content)) || !WriteBmp(g_Bmp, 64, 48) ||
        !WriteBytes(g_Png, g_PngData, sizeof(g_PngData)))
    {
        skip("cannot create test files\n");
        CoUninitialize();
        return;
    }

    TestTextFile();
    TestImageFile();
    TestPngFile();
    TestFolder();

    DeleteFileW(g_Txt);
    DeleteFileW(g_Bmp);
    DeleteFileW(g_Png);
    RemoveDirectoryW(g_Dir);
    CoUninitialize();
}
