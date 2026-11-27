/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the version comctl32 5 and comctl32 6 report through DllGetVersion
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <shlwapi.h>

typedef HRESULT (WINAPI *PFN_DLLGETVERSION)(DLLVERSIONINFO *);

static void TestVersion(HMODULE Module, LPCSTR Name, DWORD Major, DWORD Minor)
{
    PFN_DLLGETVERSION pDllGetVersion;
    DLLVERSIONINFO2 Info2;
    DLLVERSIONINFO Info;
    HRESULT Hr;

    ok(Module != NULL, "%s: not loaded, error %lu\n", Name, GetLastError());
    if (!Module)
        return;
    pDllGetVersion = (PFN_DLLGETVERSION)GetProcAddress(Module, "DllGetVersion");
    ok(pDllGetVersion != NULL, "%s: DllGetVersion not exported\n", Name);
    if (!pDllGetVersion)
        return;

    memset(&Info, 0xCC, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Hr = pDllGetVersion(&Info);
    ok(Hr == S_OK, "%s: DllGetVersion hr 0x%08lx\n", Name, Hr);
    ok(Info.cbSize == sizeof(Info), "%s: cbSize %lu\n", Name, Info.cbSize);
    ok(Info.dwMajorVersion == Major, "%s: major %lu, expected %lu\n", Name, Info.dwMajorVersion, Major);
    ok(Info.dwMinorVersion == Minor, "%s: minor %lu, expected %lu\n", Name, Info.dwMinorVersion, Minor);
    ok(Info.dwBuildNumber == 10586, "%s: build %lu\n", Name, Info.dwBuildNumber);
    ok(Info.dwPlatformID == DLLVER_PLATFORM_NT, "%s: platform %lu\n", Name, Info.dwPlatformID);

    memset(&Info2, 0xCC, sizeof(Info2));
    Info2.info1.cbSize = sizeof(Info2);
    Hr = pDllGetVersion(&Info2.info1);
    ok(Hr == S_OK, "%s: DllGetVersion with DLLVERSIONINFO2 hr 0x%08lx\n", Name, Hr);
    ok(Info2.info1.dwMajorVersion == Major, "%s: version 2 major %lu\n", Name, Info2.info1.dwMajorVersion);
    ok(Info2.info1.dwMinorVersion == Minor, "%s: version 2 minor %lu\n", Name, Info2.info1.dwMinorVersion);
    ok(Info2.info1.dwBuildNumber == 10586, "%s: version 2 build %lu\n", Name, Info2.info1.dwBuildNumber);
    ok(Info2.info1.dwPlatformID == DLLVER_PLATFORM_NT, "%s: version 2 platform %lu\n", Name, Info2.info1.dwPlatformID);
    ok((Info2.ullVersion & ~(ULONGLONG)0xFFFF) == (MAKEDLLVERULL(Major, Minor, 10586, 0)),
       "%s: ullVersion 0x%I64x\n", Name, Info2.ullVersion);

    memset(&Info, 0xCC, sizeof(Info));
    Info.cbSize = sizeof(Info) - 1;
    Hr = pDllGetVersion(&Info);
    ok(Hr == E_INVALIDARG, "%s: short cbSize hr 0x%08lx\n", Name, Hr);
    ok(Info.dwMajorVersion == 0xCCCCCCCC, "%s: short cbSize wrote major %lu\n", Name, Info.dwMajorVersion);
}

static HANDLE ActivateCommonControls6(ULONG_PTR *Cookie)
{
    static const char Manifest[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
        "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" "
        "version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
        "</dependentAssembly></dependency></assembly>";
    ACTCTXW Context = { sizeof(Context) };
    WCHAR Path[MAX_PATH];
    HANDLE File, ActCtx;
    DWORD Written;

    GetTempPathW(ARRAYSIZE(Path), Path);
    lstrcatW(Path, L"DllGetVersion.manifest");
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    WriteFile(File, Manifest, sizeof(Manifest) - 1, &Written, NULL);
    CloseHandle(File);

    Context.lpSource = Path;
    ActCtx = CreateActCtxW(&Context);
    DeleteFileW(Path);
    ok(ActCtx != INVALID_HANDLE_VALUE, "CreateActCtx failed with %lu\n", GetLastError());
    if (ActCtx != INVALID_HANDLE_VALUE && !ActivateActCtx(ActCtx, Cookie))
    {
        ok(FALSE, "ActivateActCtx failed with %lu\n", GetLastError());
        ReleaseActCtx(ActCtx);
        return INVALID_HANDLE_VALUE;
    }
    return ActCtx;
}

START_TEST(DllGetVersion)
{
    WCHAR Path[MAX_PATH];
    ULONG_PTR Cookie;
    HMODULE Module;
    HANDLE ActCtx;

    GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    lstrcatW(Path, L"\\comctl32.dll");
    Module = LoadLibraryW(Path);
    TestVersion(Module, "comctl32 5", 5, 82);
    if (Module)
        FreeLibrary(Module);

    ActCtx = ActivateCommonControls6(&Cookie);
    if (ActCtx == INVALID_HANDLE_VALUE)
        return;
    Module = LoadLibraryW(L"comctl32.dll");
    TestVersion(Module, "comctl32 6", 6, 16);
    if (Module)
        FreeLibrary(Module);
    DeactivateActCtx(0, Cookie);
    ReleaseActCtx(ActCtx);
}
