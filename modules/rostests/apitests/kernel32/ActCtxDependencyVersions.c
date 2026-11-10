/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test an application manifest that names one dependent assembly more than once
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"

#define COMMON_CONTROLS(Version) \
    "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" " \
    "name=\"Microsoft.Windows.Common-Controls\" version=\"" Version "\" " \
    "processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>" \
    "</dependentAssembly></dependency>"

static const struct
{
    const char *Name;
    const char *Dependencies;
    BOOL Expected;
} Tests[] =
{
    { "6.0.0.0",            COMMON_CONTROLS("6.0.0.0"),                                TRUE },
    { "6.0.0.0 twice",      COMMON_CONTROLS("6.0.0.0") COMMON_CONTROLS("6.0.0.0"),     TRUE },
    { "6.0.1.0, 6.0.0.0",   COMMON_CONTROLS("6.0.1.0") COMMON_CONTROLS("6.0.0.0"),     TRUE },
    { "6.0.0.0, 6.0.1.0",   COMMON_CONTROLS("6.0.0.0") COMMON_CONTROLS("6.0.1.0"),     TRUE },
    { "6.0.0.0, 5.82.0.0",  COMMON_CONTROLS("6.0.0.0") COMMON_CONTROLS("5.82.0.0"),    FALSE },
};

static void TestManifest(const char *Name, const char *Dependencies, BOOL Expected)
{
    static const char Header[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
        "<assemblyIdentity type=\"win32\" name=\"LiberNT.ActCtxDependencyVersions\" version=\"1.0.0.0\"/>";
    static const char Footer[] = "</assembly>";
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    ACTCTX_SECTION_KEYED_DATA Data = { sizeof(Data) };
    ACTCTXW ActCtx = { sizeof(ActCtx) };
    ULONG_PTR Cookie;
    HANDLE File, Context;
    DWORD Written, Error;
    BOOL Found;

    ok(GetTempPathW(ARRAYSIZE(Directory), Directory) != 0, "GetTempPathW failed: %lu\n", GetLastError());
    ok(GetTempFileNameW(Directory, L"adv", 0, Path) != 0, "GetTempFileNameW failed: %lu\n", GetLastError());

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    WriteFile(File, Header, sizeof(Header) - 1, &Written, NULL);
    WriteFile(File, Dependencies, (DWORD)strlen(Dependencies), &Written, NULL);
    WriteFile(File, Footer, sizeof(Footer) - 1, &Written, NULL);
    CloseHandle(File);

    ActCtx.lpSource = Path;
    SetLastError(0xdeadbeef);
    Context = CreateActCtxW(&ActCtx);
    Error = GetLastError();
    ok(Expected == (Context != INVALID_HANDLE_VALUE), "%s: CreateActCtxW returned %p, error %lu\n", Name, Context, Error);
    if (Context == INVALID_HANDLE_VALUE)
        ok(Error == ERROR_SXS_CANT_GEN_ACTCTX, "%s: error %lu\n", Name, Error);

    if (Context != INVALID_HANDLE_VALUE)
    {
        ok(ActivateActCtx(Context, &Cookie), "%s: ActivateActCtx failed: %lu\n", Name, GetLastError());
        Found = FindActCtxSectionStringW(0, NULL, ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION, L"comctl32.dll", &Data);
        ok(Found, "%s: comctl32.dll is not redirected: %lu\n", Name, GetLastError());
        ok(DeactivateActCtx(0, Cookie), "%s: DeactivateActCtx failed: %lu\n", Name, GetLastError());
        ReleaseActCtx(Context);
    }

    DeleteFileW(Path);
}

START_TEST(ActCtxDependencyVersions)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Tests); i++)
        TestManifest(Tests[i].Name, Tests[i].Dependencies, Tests[i].Expected);
}
