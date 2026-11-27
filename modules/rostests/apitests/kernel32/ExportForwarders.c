/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that every forwarded export of the core system DLLs resolves
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define MAX_DEPTH 8
#define MAX_MODULES 128

static const PCWSTR CoreDlls[] =
{
    L"kernel32.dll", L"kernelbase.dll", L"ntdll.dll", L"user32.dll", L"gdi32.dll", L"advapi32.dll",
    L"sechost.dll", L"ole32.dll", L"combase.dll", L"oleaut32.dll", L"shell32.dll", L"shlwapi.dll",
    L"shcore.dll", L"ws2_32.dll", L"rpcrt4.dll", L"crypt32.dll", L"bcrypt.dll", L"ucrtbase.dll",
    L"msvcrt.dll", L"imagehlp.dll", L"dbghelp.dll", L"authz.dll", L"setupapi.dll", L"cfgmgr32.dll",
    L"version.dll", L"winmm.dll", L"imm32.dll", L"uxtheme.dll", L"dwmapi.dll", L"winhttp.dll",
    L"wininet.dll", L"urlmon.dll", L"iphlpapi.dll", L"dnsapi.dll", L"netapi32.dll", L"userenv.dll",
    L"secur32.dll", L"sspicli.dll", L"wintrust.dll", L"lz32.dll", L"sfc.dll", L"powrprof.dll",
    L"propsys.dll", L"dxgi.dll", L"d3d11.dll", L"comdlg32.dll", L"winspool.drv", L"mpr.dll",
};

typedef struct _MODULE_ENTRY
{
    WCHAR Name[MAX_PATH];
    HMODULE Module;
} MODULE_ENTRY;

static MODULE_ENTRY Modules[MAX_MODULES];
static ULONG ModuleCount;

static
PIMAGE_EXPORT_DIRECTORY
GetExports(
    _In_ HMODULE Module,
    _Out_ PULONG Size)
{
    PUCHAR Base = (PUCHAR)((ULONG_PTR)Module & ~(ULONG_PTR)3);

    *Size = 0;
    return RtlImageDirectoryEntryToData(Base, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, Size);
}

static
HMODULE
OpenImage(
    _In_ PCWSTR Name)
{
    WCHAR Path[MAX_PATH];
    ULONG Index;

    for (Index = 0; Index < ModuleCount; Index++)
    {
        if (!_wcsicmp(Modules[Index].Name, Name))
            return Modules[Index].Module;
    }
    if (ModuleCount == MAX_MODULES)
        return NULL;

    GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    wcscat(Path, L"\\");
    wcscat(Path, Name);
    lstrcpynW(Modules[ModuleCount].Name, Name, MAX_PATH);
    Modules[ModuleCount].Module = LoadLibraryExW(Path, NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    return Modules[ModuleCount++].Module;
}

static
BOOLEAN
Resolve(
    _In_ PCWSTR ModuleName,
    _In_ PCSTR Export,
    _In_ ULONG Depth)
{
    PIMAGE_EXPORT_DIRECTORY Directory;
    PUCHAR Base;
    PULONG Names, Functions;
    PUSHORT Ordinals;
    HMODULE Module;
    ULONG Size, Index;

    if (Depth > MAX_DEPTH)
        return FALSE;
    Module = OpenImage(ModuleName);
    if (!Module)
        return FALSE;
    Directory = GetExports(Module, &Size);
    if (!Directory)
        return FALSE;
    Base = (PUCHAR)((ULONG_PTR)Module & ~(ULONG_PTR)3);
    Names = (PULONG)(Base + Directory->AddressOfNames);
    Functions = (PULONG)(Base + Directory->AddressOfFunctions);
    Ordinals = (PUSHORT)(Base + Directory->AddressOfNameOrdinals);

    for (Index = 0; Index < Directory->NumberOfNames; Index++)
    {
        ULONG Rva;

        if (strcmp((PCSTR)(Base + Names[Index]), Export))
            continue;
        Rva = Functions[Ordinals[Index]];
        if (Rva >= (ULONG)((PUCHAR)Directory - Base) && Rva < (ULONG)((PUCHAR)Directory - Base) + Size)
        {
            CHAR Forward[MAX_PATH];
            WCHAR Target[MAX_PATH];
            PSTR Dot;

            lstrcpynA(Forward, (PCSTR)(Base + Rva), sizeof(Forward));
            Dot = strrchr(Forward, '.');
            if (!Dot)
                return FALSE;
            *Dot = ANSI_NULL;
            if (!_strnicmp(Forward, "api-", 4) || !_strnicmp(Forward, "ext-", 4) || Dot[1] == '#')
                return TRUE;
            if (sizeof(PVOID) == 8 && Dot[1] == '_' && strchr(Dot + 1, '@'))
                return TRUE;
            MultiByteToWideChar(CP_ACP, 0, Forward, -1, Target, ARRAYSIZE(Target));
            if (!wcschr(Target, L'.'))
                wcscat(Target, L".dll");
            return Resolve(Target, Dot + 1, Depth + 1);
        }
        return Rva != 0;
    }
    return FALSE;
}

START_TEST(ExportForwarders)
{
    ULONG Index, Checked = 0, Broken = 0;

    for (Index = 0; Index < ARRAYSIZE(CoreDlls); Index++)
    {
        PIMAGE_EXPORT_DIRECTORY Directory;
        PUCHAR Base;
        PULONG Names, Functions;
        PUSHORT Ordinals;
        HMODULE Module;
        ULONG Size, Name;

        Module = OpenImage(CoreDlls[Index]);
        if (!Module)
            continue;
        Directory = GetExports(Module, &Size);
        if (!Directory)
            continue;
        Base = (PUCHAR)((ULONG_PTR)Module & ~(ULONG_PTR)3);
        Names = (PULONG)(Base + Directory->AddressOfNames);
        Functions = (PULONG)(Base + Directory->AddressOfFunctions);
        Ordinals = (PUSHORT)(Base + Directory->AddressOfNameOrdinals);
        for (Name = 0; Name < Directory->NumberOfNames; Name++)
        {
            ULONG Rva = Functions[Ordinals[Name]];
            PCSTR Export = (PCSTR)(Base + Names[Name]);

            if (Rva < (ULONG)((PUCHAR)Directory - Base) || Rva >= (ULONG)((PUCHAR)Directory - Base) + Size)
                continue;
            Checked++;
            if (!Resolve(CoreDlls[Index], Export, 0))
            {
                Broken++;
                ok(0, "%ls!%s forwards to %s, which does not resolve\n", CoreDlls[Index], Export, (PCSTR)(Base + Rva));
            }
        }
    }

    ok(Checked != 0, "no forwarded export was found\n");
    trace("%lu forwarded exports checked, %lu broken\n", Checked, Broken);
    for (Index = 0; Index < ModuleCount; Index++)
    {
        if (Modules[Index].Module)
            FreeLibrary(Modules[Index].Module);
    }
}
