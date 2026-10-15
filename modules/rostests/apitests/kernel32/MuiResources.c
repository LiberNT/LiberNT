/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests resources of language-neutral modules and their MUI files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winuser.h>

typedef BOOL (WINAPI *PFN_GETFILEMUIPATH)(DWORD, PCWSTR, PWSTR, PULONG, PWSTR, PULONG, PULONGLONG);
typedef BOOL (WINAPI *PFN_GETFILEMUIINFO)(DWORD, PCWSTR, PFILEMUIINFO, DWORD *);
typedef BOOL (WINAPI *PFN_ENUMRESOURCETYPESEXW)(HMODULE, ENUMRESTYPEPROCW, LONG_PTR, DWORD, LANGID);
typedef BOOL (WINAPI *PFN_ENUMRESOURCENAMESEXW)(HMODULE, LPCWSTR, ENUMRESNAMEPROCW, LONG_PTR, DWORD, LANGID);
typedef BOOL (WINAPI *PFN_ENUMRESOURCELANGUAGESEXW)(HMODULE, LPCWSTR, LPCWSTR, ENUMRESLANGPROCW, LONG_PTR, DWORD, LANGID);

static PFN_GETFILEMUIPATH pGetFileMUIPath;
static PFN_GETFILEMUIINFO pGetFileMUIInfo;
static PFN_ENUMRESOURCETYPESEXW pEnumResourceTypesExW;
static PFN_ENUMRESOURCENAMESEXW pEnumResourceNamesExW;
static PFN_ENUMRESOURCELANGUAGESEXW pEnumResourceLanguagesExW;

static WCHAR TestDir[MAX_PATH];
static WCHAR LnPath[MAX_PATH];
static WCHAR MuiPath[MAX_PATH];
static WCHAR NoMuiPath[MAX_PATH];
static WCHAR TextPath[MAX_PATH];

typedef struct _ENUM_LIST
{
    WCHAR Text[512];
    HMODULE Module;
    BOOL ModuleMatches;
    ULONG Count;
} ENUM_LIST, *PENUM_LIST;

static VOID
AppendItem(PENUM_LIST List, HMODULE Module, LPCWSTR Name)
{
    WCHAR Item[64];

    if (IS_INTRESOURCE(Name))
        StringCchPrintfW(Item, _countof(Item), L"%u", (UINT)(ULONG_PTR)Name);
    else
        StringCchCopyW(Item, _countof(Item), Name);
    if (List->Text[0])
        StringCchCatW(List->Text, _countof(List->Text), L",");
    StringCchCatW(List->Text, _countof(List->Text), Item);
    if (Module != List->Module)
        List->ModuleMatches = FALSE;
    List->Count++;
}

static VOID
InitList(PENUM_LIST List, HMODULE Module)
{
    List->Text[0] = UNICODE_NULL;
    List->Module = Module;
    List->ModuleMatches = TRUE;
    List->Count = 0;
}

static BOOL CALLBACK
TypeProc(HMODULE Module, LPWSTR Type, LONG_PTR Param)
{
    AppendItem((PENUM_LIST)Param, Module, Type);
    return TRUE;
}

static BOOL CALLBACK
NameProc(HMODULE Module, LPCWSTR Type, LPWSTR Name, LONG_PTR Param)
{
    AppendItem((PENUM_LIST)Param, Module, Name);
    return TRUE;
}

static BOOL CALLBACK
LangProc(HMODULE Module, LPCWSTR Type, LPCWSTR Name, WORD Language, LONG_PTR Param)
{
    AppendItem((PENUM_LIST)Param, Module, MAKEINTRESOURCEW(Language));
    return TRUE;
}

static BOOL CALLBACK
StopTypeProc(HMODULE Module, LPWSTR Type, LONG_PTR Param)
{
    AppendItem((PENUM_LIST)Param, Module, Type);
    return FALSE;
}

static BOOL
WriteTextFile(PCWSTR Path, PCSTR Text)
{
    HANDLE File;
    DWORD Written;
    BOOL Ret;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ret = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL);
    CloseHandle(File);
    return Ret;
}

static BOOL
SetupTestFiles(VOID)
{
    WCHAR SourceDir[MAX_PATH], Source[MAX_PATH], Path[MAX_PATH];
    PWSTR Slash;

    GetModuleFileNameW(NULL, SourceDir, _countof(SourceDir));
    Slash = wcsrchr(SourceDir, L'\\');
    if (!Slash)
        return FALSE;
    Slash[1] = UNICODE_NULL;

    GetTempPathW(_countof(TestDir), TestDir);
    StringCchCatW(TestDir, _countof(TestDir), L"k32muitest");
    CreateDirectoryW(TestDir, NULL);

    StringCchPrintfW(LnPath, _countof(LnPath), L"%s\\kernel32_muitest.dll", TestDir);
    StringCchPrintfW(MuiPath, _countof(MuiPath), L"%s\\en-US\\kernel32_muitest.dll.mui", TestDir);
    StringCchPrintfW(NoMuiPath, _countof(NoMuiPath), L"%s\\nomui\\kernel32_muitest.dll", TestDir);
    StringCchPrintfW(TextPath, _countof(TextPath), L"%s\\notes.txt", TestDir);

    StringCchPrintfW(Source, _countof(Source), L"%skernel32_muitest.dll", SourceDir);
    if (!CopyFileW(Source, LnPath, FALSE))
        return FALSE;

    StringCchPrintfW(Path, _countof(Path), L"%s\\nomui", TestDir);
    CreateDirectoryW(Path, NULL);
    if (!CopyFileW(Source, NoMuiPath, FALSE))
        return FALSE;

    StringCchPrintfW(Source, _countof(Source), L"%sen-US\\kernel32_muitest.dll.mui", SourceDir);
    StringCchPrintfW(Path, _countof(Path), L"%s\\en-US", TestDir);
    CreateDirectoryW(Path, NULL);
    if (!CopyFileW(Source, MuiPath, FALSE))
        return FALSE;

    StringCchPrintfW(Path, _countof(Path), L"%s\\de-DE", TestDir);
    CreateDirectoryW(Path, NULL);
    StringCchPrintfW(Path, _countof(Path), L"%s\\de-DE\\kernel32_muitest.dll.mui", TestDir);
    if (!CopyFileW(Source, Path, FALSE))
        return FALSE;

    if (!WriteTextFile(TextPath, "neutral"))
        return FALSE;
    StringCchPrintfW(Path, _countof(Path), L"%s\\en-US\\notes.txt", TestDir);
    return WriteTextFile(Path, "english");
}

static VOID
CleanupTestFiles(VOID)
{
    static const PCWSTR Files[] =
    {
        L"kernel32_muitest.dll",
        L"en-US\\kernel32_muitest.dll.mui",
        L"de-DE\\kernel32_muitest.dll.mui",
        L"nomui\\kernel32_muitest.dll",
        L"notes.txt",
        L"en-US\\notes.txt",
    };
    static const PCWSTR Dirs[] = { L"en-US", L"de-DE", L"nomui" };
    WCHAR Path[MAX_PATH];
    ULONG i;

    for (i = 0; i < _countof(Files); i++)
    {
        StringCchPrintfW(Path, _countof(Path), L"%s\\%s", TestDir, Files[i]);
        DeleteFileW(Path);
    }
    for (i = 0; i < _countof(Dirs); i++)
    {
        StringCchPrintfW(Path, _countof(Path), L"%s\\%s", TestDir, Dirs[i]);
        RemoveDirectoryW(Path);
    }
    RemoveDirectoryW(TestDir);
}

static PCWSTR
ResourceText(HMODULE Module, LPCWSTR Type, LPCWSTR Name, WORD Language, PDWORD Error)
{
    HRSRC Resource;
    HGLOBAL Data;

    SetLastError(0xdeadbeef);
    if (Language == 0xffff)
        Resource = FindResourceW(Module, Name, Type);
    else
        Resource = FindResourceExW(Module, Type, Name, Language);
    *Error = GetLastError();
    if (!Resource)
        return NULL;
    Data = LoadResource(Module, Resource);
    return Data ? (PCWSTR)LockResource(Data) : NULL;
}

static VOID
TestResourceText(HMODULE Module, PCSTR Tag, LPCWSTR Type, LPCWSTR Name, PCWSTR Expected)
{
    PCWSTR Text;
    DWORD Error;

    Text = ResourceText(Module, Type, Name, 0xffff, &Error);
    ok(Text != NULL, "%s: resource %p/%p not found, error %lu\n", Tag, Type, Name, Error);
    if (Text)
        ok(!wcscmp(Text, Expected), "%s: resource %p/%p is %s\n", Tag, Type, Name, wine_dbgstr_w(Text));
}

static VOID
TestMissingResource(HMODULE Module, PCSTR Tag, LPCWSTR Type, LPCWSTR Name, WORD Language, DWORD ExpectedError)
{
    PCWSTR Text;
    DWORD Error;

    Text = ResourceText(Module, Type, Name, Language, &Error);
    ok(Text == NULL, "%s: resource %p/%p/%04x found\n", Tag, Type, Name, Language);
    ok(Error == ExpectedError, "%s: resource %p/%p/%04x error %lu\n", Tag, Type, Name, Language, Error);
}

static BOOL
IsInModuleMapping(HMODULE Module, LPCVOID Pointer)
{
    MEMORY_BASIC_INFORMATION Info;

    if (!Pointer || !VirtualQuery(Pointer, &Info, sizeof(Info)))
        return FALSE;
    return Info.AllocationBase == (PVOID)((ULONG_PTR)Module & ~(ULONG_PTR)3);
}

static VOID
TestLoader(HMODULE Module, PCSTR Tag)
{
    WCHAR Buffer[64];
    PCWSTR Text;
    DWORD Error;
    int Length;

    SetLastError(0xdeadbeef);
    Length = LoadStringW(Module, 1, Buffer, _countof(Buffer));
    ok(Length == 14, "%s: LoadStringW returned %d, error %lu\n", Tag, Length, GetLastError());
    if (Length > 0)
        ok(!wcscmp(Buffer, L"mui string one"), "%s: string is %s\n", Tag, wine_dbgstr_w(Buffer));

    TestResourceText(Module, Tag, (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(1), L"ln one");
    TestResourceText(Module, Tag, (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(2), L"mui two");
    TestResourceText(Module, Tag, (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(3), L"mui three");
    TestResourceText(Module, Tag, L"ZLNTYPE", MAKEINTRESOURCEW(1), L"ln custom");
    TestResourceText(Module, Tag, L"ZMUITYPE", MAKEINTRESOURCEW(1), L"mui custom");

    Text = ResourceText(Module, (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(3), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), &Error);
    ok(Text && !wcscmp(Text, L"mui three"), "%s: en-US resource %s, error %lu\n", Tag, wine_dbgstr_w(Text), Error);
    ok(Text && !IsInModuleMapping(Module, Text), "%s: MUI resource is in the module mapping\n", Tag);

    Text = ResourceText(Module, (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(1), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), &Error);
    ok(Text && !wcscmp(Text, L"ln one"), "%s: en-US neutral resource %s, error %lu\n", Tag, wine_dbgstr_w(Text), Error);
    ok(Text && IsInModuleMapping(Module, Text), "%s: neutral resource is outside the module mapping\n", Tag);

    Text = ResourceText(Module, (LPCWSTR)RT_VERSION, MAKEINTRESOURCEW(1), 0xffff, &Error);
    ok(Text != NULL, "%s: version resource not found, error %lu\n", Tag, Error);
    ok(Text && IsInModuleMapping(Module, Text), "%s: version resource is outside the module mapping\n", Tag);

    Text = ResourceText(Module, L"MUI", MAKEINTRESOURCEW(1), 0xffff, &Error);
    ok(Text && IsInModuleMapping(Module, Text), "%s: configuration %p is not the module one, error %lu\n", Tag, Text, Error);
    ok(Text && ((PULONG)Text)[4] == 0x11, "%s: configuration file type %lx\n", Tag, Text ? ((PULONG)Text)[4] : 0);

    TestMissingResource(Module, Tag, (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(9), 0xffff, ERROR_RESOURCE_NAME_NOT_FOUND);
    TestMissingResource(Module, Tag, MAKEINTRESOURCEW(77), MAKEINTRESOURCEW(1), 0xffff, ERROR_RESOURCE_TYPE_NOT_FOUND);
    TestMissingResource(Module, Tag, (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(3), MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), ERROR_MUI_FILE_NOT_FOUND);
}

static VOID
TestTypes(HMODULE Module, PCSTR Tag, DWORD Flags, PCWSTR Expected)
{
    ENUM_LIST List;
    BOOL Ret;

    InitList(&List, Module);
    SetLastError(0xdeadbeef);
    if (Flags == 0xffffffff)
        Ret = EnumResourceTypesW(Module, TypeProc, (LONG_PTR)&List);
    else if (pEnumResourceTypesExW)
        Ret = pEnumResourceTypesExW(Module, TypeProc, (LONG_PTR)&List, Flags, 0);
    else
    {
        skip("EnumResourceTypesExW is not available\n");
        return;
    }
    ok(Ret, "%s: types flags %lx failed, error %lu\n", Tag, Flags, GetLastError());
    ok(!wcscmp(List.Text, Expected), "%s: types flags %lx are %s\n", Tag, Flags, wine_dbgstr_w(List.Text));
    ok(List.ModuleMatches, "%s: types flags %lx got another module\n", Tag, Flags);
}

static VOID
TestNames(HMODULE Module, PCSTR Tag, LPCWSTR Type, DWORD Flags, PCWSTR Expected)
{
    ENUM_LIST List;
    BOOL Ret;

    InitList(&List, Module);
    SetLastError(0xdeadbeef);
    if (Flags == 0xffffffff)
        Ret = EnumResourceNamesW(Module, Type, NameProc, (LONG_PTR)&List);
    else if (pEnumResourceNamesExW)
        Ret = pEnumResourceNamesExW(Module, Type, NameProc, (LONG_PTR)&List, Flags, 0);
    else
    {
        skip("EnumResourceNamesExW is not available\n");
        return;
    }
    ok(Ret, "%s: names %p flags %lx failed, error %lu\n", Tag, Type, Flags, GetLastError());
    ok(!wcscmp(List.Text, Expected), "%s: names %p flags %lx are %s\n", Tag, Type, Flags, wine_dbgstr_w(List.Text));
    ok(List.ModuleMatches, "%s: names %p flags %lx got another module\n", Tag, Type, Flags);
}

static VOID
TestLanguages(HMODULE Module, PCSTR Tag, LPCWSTR Type, LPCWSTR Name, DWORD Flags, PCWSTR Expected)
{
    ENUM_LIST List;
    BOOL Ret;

    InitList(&List, Module);
    SetLastError(0xdeadbeef);
    if (Flags == 0xffffffff)
        Ret = EnumResourceLanguagesW(Module, Type, Name, LangProc, (LONG_PTR)&List);
    else if (pEnumResourceLanguagesExW)
        Ret = pEnumResourceLanguagesExW(Module, Type, Name, LangProc, (LONG_PTR)&List, Flags, 0);
    else
    {
        skip("EnumResourceLanguagesExW is not available\n");
        return;
    }
    ok(Ret, "%s: languages %p/%p flags %lx failed, error %lu\n", Tag, Type, Name, Flags, GetLastError());
    ok(!wcscmp(List.Text, Expected), "%s: languages %p/%p flags %lx are %s\n", Tag, Type, Name, Flags, wine_dbgstr_w(List.Text));
}

static VOID
TestEnumeration(HMODULE Module, PCSTR Tag)
{
    ENUM_LIST List;
    BOOL Ret;

    TestTypes(Module, Tag, 0xffffffff, L"MUI,ZLNTYPE,ZMUITYPE,6,10,16");
    TestTypes(Module, Tag, 0, L"MUI,ZLNTYPE,ZMUITYPE,6,10,16");
    TestTypes(Module, Tag, RESOURCE_ENUM_LN, L"MUI,ZLNTYPE,10,16");
    TestTypes(Module, Tag, RESOURCE_ENUM_MUI, L"ZMUITYPE,6,10,16");

    TestNames(Module, Tag, (LPCWSTR)RT_RCDATA, 0xffffffff, L"1,2,3");
    TestNames(Module, Tag, (LPCWSTR)RT_RCDATA, RESOURCE_ENUM_LN, L"1,2");
    TestNames(Module, Tag, (LPCWSTR)RT_RCDATA, RESOURCE_ENUM_MUI, L"2,3");
    TestNames(Module, Tag, (LPCWSTR)RT_STRING, 0xffffffff, L"1");
    TestNames(Module, Tag, (LPCWSTR)RT_VERSION, 0xffffffff, L"1");
    TestNames(Module, Tag, L"ZMUITYPE", 0xffffffff, L"1");
    TestNames(Module, Tag, L"ZLNTYPE", 0xffffffff, L"1");

    TestLanguages(Module, Tag, (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(2), 0xffffffff, L"1033");
    TestLanguages(Module, Tag, (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(3), 0xffffffff, L"1033");

    InitList(&List, Module);
    SetLastError(0xdeadbeef);
    Ret = EnumResourceNamesW(Module, MAKEINTRESOURCEW(77), NameProc, (LONG_PTR)&List);
    ok(!Ret, "%s: names of a missing type succeeded\n", Tag);
    ok(GetLastError() == ERROR_RESOURCE_TYPE_NOT_FOUND, "%s: names of a missing type error %lu\n", Tag, GetLastError());

    InitList(&List, Module);
    SetLastError(0xdeadbeef);
    Ret = EnumResourceTypesW(Module, StopTypeProc, (LONG_PTR)&List);
    ok(!Ret, "%s: stopped type enumeration succeeded\n", Tag);
    ok(GetLastError() == ERROR_RESOURCE_ENUM_USER_STOP, "%s: stopped type enumeration error %lu\n", Tag, GetLastError());
    ok(List.Count == 1, "%s: stopped type enumeration called back %lu times\n", Tag, List.Count);
}

static VOID
TestNoMui(VOID)
{
    WCHAR Buffer[64];
    HMODULE Module;
    int Length;

    Module = LoadLibraryExW(NoMuiPath, NULL, LOAD_LIBRARY_AS_DATAFILE);
    ok(Module != NULL, "nomui: load failed, error %lu\n", GetLastError());
    if (!Module)
        return;

    SetLastError(0xdeadbeef);
    Length = LoadStringW(Module, 1, Buffer, _countof(Buffer));
    ok(Length == 0, "nomui: LoadStringW returned %d\n", Length);
    ok(GetLastError() == ERROR_MUI_FILE_NOT_FOUND, "nomui: LoadStringW error %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Length = LoadStringW(Module, 1, Buffer, _countof(Buffer));
    ok(Length == 0, "nomui: second LoadStringW returned %d\n", Length);
    ok(GetLastError() == ERROR_MUI_FILE_NOT_LOADED, "nomui: second LoadStringW error %lu\n", GetLastError());

    TestResourceText(Module, "nomui", (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(1), L"ln one");
    TestMissingResource(Module, "nomui", (LPCWSTR)RT_RCDATA, MAKEINTRESOURCEW(3), 0xffff, ERROR_MUI_FILE_NOT_LOADED);
    TestTypes(Module, "nomui", 0xffffffff, L"MUI,ZLNTYPE,10,16");
    TestNames(Module, "nomui", (LPCWSTR)RT_RCDATA, 0xffffffff, L"1,2");

    FreeLibrary(Module);
}

static VOID
TestFileMuiInfo(PCWSTR Path, PCSTR Tag, DWORD ExpectedType, ULONG MainIds, ULONG MuiIds)
{
    BYTE Buffer[1024];
    PFILEMUIINFO Info = (PFILEMUIINFO)Buffer;
    DWORD Size, Flags = MUI_QUERY_TYPE | MUI_QUERY_CHECKSUM | MUI_QUERY_LANGUAGE_NAME | MUI_QUERY_RESOURCE_TYPES;
    ULONG i;
    BOOL Ret;

    if (!pGetFileMUIInfo)
    {
        skip("GetFileMUIInfo is not available\n");
        return;
    }

    Size = 0;
    SetLastError(0xdeadbeef);
    Ret = pGetFileMUIInfo(Flags, Path, NULL, &Size);
    ok(!Ret, "%s: size query succeeded\n", Tag);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "%s: size query error %lu\n", Tag, GetLastError());
    ok(Size == FIELD_OFFSET(FILEMUIINFO, abBuffer) + sizeof(L"en-US"), "%s: required size %lu\n", Tag, Size);

    ZeroMemory(Buffer, sizeof(Buffer));
    Info->dwSize = sizeof(Buffer);
    Info->dwVersion = MUI_FILEINFO_VERSION;
    Size = sizeof(Buffer);
    SetLastError(0xdeadbeef);
    Ret = pGetFileMUIInfo(Flags, Path, Info, &Size);
    ok(Ret, "%s: query failed, error %lu\n", Tag, GetLastError());
    if (!Ret)
        return;

    ok(Info->dwFileType == ExpectedType, "%s: file type %lu\n", Tag, Info->dwFileType);
    for (i = 0; i < 16; i++)
    {
        if (Info->pChecksum[i] != 0x11 || Info->pServiceChecksum[i] != 0x22)
            break;
    }
    ok(i == 16, "%s: checksums differ at %lu\n", Tag, i);
    ok(Info->dwLanguageNameOffset && !wcscmp((PCWSTR)(Buffer + Info->dwLanguageNameOffset), L"en-US"),
       "%s: language offset %lu\n", Tag, Info->dwLanguageNameOffset);
    ok(Info->dwTypeIDMainSize == MainIds, "%s: main type ids %lu\n", Tag, Info->dwTypeIDMainSize);
    ok(Info->dwTypeIDMUISize == MuiIds, "%s: MUI type ids %lu\n", Tag, Info->dwTypeIDMUISize);
    trace("%s: offsets lang %lu main ids %lu names %lu MUI ids %lu names %lu\n", Tag,
          Info->dwLanguageNameOffset, Info->dwTypeIDMainOffset, Info->dwTypeNameMainOffset,
          Info->dwTypeIDMUIOffset, Info->dwTypeNameMUIOffset);
}

static VOID
FillPattern(PWSTR Buffer, ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count - 1; i++)
        Buffer[i] = L'X';
    Buffer[Count - 1] = UNICODE_NULL;
}

static BOOL
CallMuiPath(DWORD Flags, PCWSTR File, PWSTR Language, ULONG LanguageLength, PWSTR Path, ULONG PathLength,
            PULONGLONG Enumerator, PULONG OutLanguageLength, PULONG OutPathLength, PDWORD Error)
{
    BOOL Ret;

    *OutLanguageLength = LanguageLength;
    *OutPathLength = PathLength;
    SetLastError(0xdeadbeef);
    Ret = pGetFileMUIPath(Flags, File, Language, OutLanguageLength, Path, OutPathLength, Enumerator);
    *Error = GetLastError();
    return Ret;
}

static VOID
TestEnumeratePaths(DWORD Flags, PCWSTR File, PCSTR Tag, PCWSTR Expected)
{
    WCHAR Language[LOCALE_NAME_MAX_LENGTH], Path[MAX_PATH], Seen[512];
    ULONGLONG Enumerator = 0;
    ULONG LanguageLength, PathLength, Calls;
    DWORD Error;
    BOOL Ret;

    Seen[0] = UNICODE_NULL;
    for (Calls = 0; Calls < 10; Calls++)
    {
        Language[0] = UNICODE_NULL;
        Ret = CallMuiPath(Flags, File, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                          &LanguageLength, &PathLength, &Error);
        if (!Ret)
            break;
        if (Seen[0])
            StringCchCatW(Seen, _countof(Seen), L",");
        StringCchCatW(Seen, _countof(Seen), Language);
    }
    ok(!Ret && Error == ERROR_NO_MORE_FILES, "%s: enumeration ended with %d error %lu\n", Tag, Ret, Error);
    ok(!wcscmp(Seen, Expected), "%s: languages %s\n", Tag, wine_dbgstr_w(Seen));
}

static VOID
TestFileMuiPath(VOID)
{
    WCHAR Language[LOCALE_NAME_MAX_LENGTH], Path[MAX_PATH], Expected[MAX_PATH];
    ULONGLONG Enumerator;
    ULONG LanguageLength, PathLength;
    DWORD Error;
    BOOL Ret;

    if (!pGetFileMUIPath)
    {
        skip("GetFileMUIPath is not available\n");
        return;
    }

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, LnPath, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(Ret, "name: failed, error %lu\n", Error);
    if (Ret)
    {
        ok(Error == 0xdeadbeef, "name: error %lu\n", Error);
        ok(!wcscmp(Language, L"en-US"), "name: language %s\n", wine_dbgstr_w(Language));
        ok(!_wcsicmp(Path, MuiPath), "name: path %s\n", wine_dbgstr_w(Path));
        ok(LanguageLength == _countof(Language), "name: language length %lu\n", LanguageLength);
        ok(PathLength == _countof(Path), "name: path length %lu\n", PathLength);
        trace("name: enumerator %I64u\n", Enumerator);
    }
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, LnPath, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(!Ret, "name next: succeeded with %s\n", wine_dbgstr_w(Path));
    ok(Error == ERROR_NO_MORE_FILES, "name next: error %lu\n", Error);

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_ID, LnPath, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(Ret, "id: failed, error %lu\n", Error);
    if (Ret)
    {
        ok(!wcscmp(Language, L"0409"), "id: language %s\n", wine_dbgstr_w(Language));
        ok(!_wcsicmp(Path, MuiPath), "id: path %s\n", wine_dbgstr_w(Path));
        ok(LanguageLength == _countof(Language), "id: language length %lu\n", LanguageLength);
    }

    Enumerator = 0;
    StringCchCopyW(Language, _countof(Language), L"de-DE");
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, LnPath, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(Ret, "de-DE: failed, error %lu\n", Error);
    if (Ret)
    {
        ok(!_wcsicmp(Path, MuiPath), "de-DE: path %s\n", wine_dbgstr_w(Path));
        ok(!wcscmp(Language, L"en-US"), "de-DE: language %s\n", wine_dbgstr_w(Language));
    }
    StringCchCopyW(Language, _countof(Language), L"de-DE");
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, LnPath, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(!Ret && Error == ERROR_NO_MORE_FILES, "de-DE next: %d error %lu\n", Ret, Error);

    Enumerator = 0;
    StringCchCopyW(Language, _countof(Language), L"fr-FR");
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, LnPath, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(Ret, "fr-FR: failed, error %lu\n", Error);
    if (Ret)
        ok(!_wcsicmp(Path, MuiPath), "fr-FR: path %s\n", wine_dbgstr_w(Path));

    Enumerator = 0;
    StringCchCopyW(Language, _countof(Language), L"040c");
    Ret = CallMuiPath(MUI_LANGUAGE_ID, LnPath, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(Ret, "040c: failed, error %lu\n", Error);
    if (Ret)
    {
        ok(!_wcsicmp(Path, MuiPath), "040c: path %s\n", wine_dbgstr_w(Path));
        ok(!wcscmp(Language, L"0409"), "040c: language %s\n", wine_dbgstr_w(Language));
    }

    Enumerator = 0;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, LnPath, NULL, 0, NULL, 0, &Enumerator, &LanguageLength, &PathLength, &Error);
    ok(Ret, "size: failed, error %lu\n", Error);
    ok(LanguageLength == LOCALE_NAME_MAX_LENGTH, "size: language length %lu\n", LanguageLength);
    ok(PathLength == wcslen(MuiPath) + 1, "size: path length %lu\n", PathLength);
    ok(Enumerator == 0, "size: enumerator %I64u\n", Enumerator);

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    FillPattern(Path, _countof(Path));
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, LnPath, Language, _countof(Language), Path, 4, &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(Ret, "small path: failed, error %lu\n", Error);
    ok(PathLength == wcslen(MuiPath) + 1, "small path: length %lu\n", PathLength);
    ok(Enumerator != 0, "small path: enumerator not advanced\n");
    trace("small path: buffer %04x %04x %04x %04x %04x language %s\n", Path[0], Path[1], Path[2], Path[3], Path[4],
          wine_dbgstr_w(Language));

    Enumerator = 0;
    FillPattern(Language, _countof(Language));
    Language[2] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, LnPath, Language, 2, Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(Ret, "small language: failed, error %lu\n", Error);
    ok(LanguageLength == 2, "small language: length %lu\n", LanguageLength);
    ok(!_wcsicmp(Path, MuiPath), "small language: path %s\n", wine_dbgstr_w(Path));
    trace("small language: buffer %04x %04x %04x\n", Language[0], Language[1], Language[2]);

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME | MUI_LANG_NEUTRAL_PE_FILE | MUI_NON_LANG_NEUTRAL_FILE, LnPath, Language,
                      _countof(Language), Path, _countof(Path), &Enumerator, &LanguageLength, &PathLength, &Error);
    ok(!Ret, "both file types: succeeded\n");
    ok(Error == ERROR_INVALID_PARAMETER, "both file types: error %lu\n", Error);

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID, LnPath, Language, _countof(Language), Path,
                      _countof(Path), &Enumerator, &LanguageLength, &PathLength, &Error);
    ok(!Ret, "both formats: succeeded\n");
    ok(Error == ERROR_INVALID_PARAMETER, "both formats: error %lu\n", Error);

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, LnPath, Language, _countof(Language), Path, _countof(Path), NULL,
                      &LanguageLength, &PathLength, &Error);
    ok(!Ret, "no enumerator: succeeded\n");
    ok(Error == ERROR_INVALID_PARAMETER, "no enumerator: error %lu\n", Error);

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(0, LnPath, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(Ret, "no flags: failed, error %lu\n", Error);
    if (Ret)
        ok(!wcscmp(Language, L"en-US"), "no flags: language %s\n", wine_dbgstr_w(Language));

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, TextPath, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    StringCchPrintfW(Expected, _countof(Expected), L"%s\\en-US\\notes.txt", TestDir);
    ok(Ret, "text: failed, error %lu\n", Error);
    if (Ret)
        ok(!_wcsicmp(Path, Expected), "text: path %s\n", wine_dbgstr_w(Path));

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME | MUI_LANG_NEUTRAL_PE_FILE, TextPath, Language, _countof(Language), Path,
                      _countof(Path), &Enumerator, &LanguageLength, &PathLength, &Error);
    ok(!Ret, "text as LN: succeeded with %s\n", wine_dbgstr_w(Path));
    ok(Error == ERROR_NO_MORE_FILES, "text as LN: error %lu\n", Error);

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME | MUI_NON_LANG_NEUTRAL_FILE, LnPath, Language, _countof(Language), Path,
                      _countof(Path), &Enumerator, &LanguageLength, &PathLength, &Error);
    ok(!Ret, "LN as text: succeeded with %s\n", wine_dbgstr_w(Path));
    ok(Error == ERROR_NO_MORE_FILES, "LN as text: error %lu\n", Error);

    Enumerator = 0;
    Language[0] = UNICODE_NULL;
    Ret = CallMuiPath(MUI_LANGUAGE_NAME, NoMuiPath, Language, _countof(Language), Path, _countof(Path), &Enumerator,
                      &LanguageLength, &PathLength, &Error);
    ok(!Ret, "nomui: succeeded with %s\n", wine_dbgstr_w(Path));
    ok(Error == ERROR_NO_MORE_FILES, "nomui: error %lu\n", Error);

    TestEnumeratePaths(MUI_LANGUAGE_NAME | MUI_USE_SEARCH_ALL_LANGUAGES, LnPath, "all", L"de-DE,en-US");
    TestEnumeratePaths(MUI_LANGUAGE_NAME | MUI_USE_INSTALLED_LANGUAGES, LnPath, "installed", L"en-US");
    TestEnumeratePaths(MUI_LANGUAGE_NAME | MUI_USER_PREFERRED_UI_LANGUAGES, LnPath, "preferred", L"en-US");
}

START_TEST(MuiResources)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE Module;

    pGetFileMUIPath = (PFN_GETFILEMUIPATH)GetProcAddress(Kernel32, "GetFileMUIPath");
    pGetFileMUIInfo = (PFN_GETFILEMUIINFO)GetProcAddress(Kernel32, "GetFileMUIInfo");
    pEnumResourceTypesExW = (PFN_ENUMRESOURCETYPESEXW)GetProcAddress(Kernel32, "EnumResourceTypesExW");
    pEnumResourceNamesExW = (PFN_ENUMRESOURCENAMESEXW)GetProcAddress(Kernel32, "EnumResourceNamesExW");
    pEnumResourceLanguagesExW = (PFN_ENUMRESOURCELANGUAGESEXW)GetProcAddress(Kernel32, "EnumResourceLanguagesExW");

    if (!SetupTestFiles())
    {
        skip("MUI test files are not available, error %lu\n", GetLastError());
        CleanupTestFiles();
        return;
    }

    Module = LoadLibraryExW(LnPath, NULL, LOAD_LIBRARY_AS_DATAFILE);
    ok(Module != NULL, "datafile: load failed, error %lu\n", GetLastError());
    if (Module)
    {
        TestLoader(Module, "datafile");
        TestEnumeration(Module, "datafile");
        FreeLibrary(Module);
    }

    Module = LoadLibraryW(LnPath);
    ok(Module != NULL, "image: load failed, error %lu\n", GetLastError());
    if (Module)
    {
        TestLoader(Module, "image");
        TestEnumeration(Module, "image");
        FreeLibrary(Module);
    }

    TestNoMui();
    TestFileMuiInfo(LnPath, "info LN", MUI_FILETYPE_LANGUAGE_NEUTRAL_MAIN, 2, 3);
    TestFileMuiInfo(MuiPath, "info MUI", MUI_FILETYPE_LANGUAGE_NEUTRAL_MUI, 0, 3);
    TestFileMuiPath();

    CleanupTestFiles();
}
