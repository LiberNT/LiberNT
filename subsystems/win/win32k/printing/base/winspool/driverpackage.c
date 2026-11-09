/*
 * PROJECT:     LiberNT Spooler API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Printer driver packages in the driver store
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include "precomp.h"
#include <setupapi.h>

extern const WCHAR wszCurrentEnvironment[];

HRESULT WINAPI DriverStoreAddDriverPackageW(PCWSTR InfPath, PVOID Reserved1, PVOID Reserved2,
                                            WORD Architecture, PWSTR ReturnPath, PDWORD ReturnLength);
HRESULT WINAPI DriverStoreDeleteDriverPackageW(PCWSTR InfPath, PVOID Reserved1, PVOID Reserved2);

#define MAX_PACKAGE_FILES 128
#define DRIVERS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Print\\Environments\\%s\\Drivers\\Version-3"

typedef struct _PACKAGE_FILES
{
    DWORD Count;
    PWSTR Names[MAX_PACKAGE_FILES];
    PWSTR Sources[MAX_PACKAGE_FILES];
} PACKAGE_FILES, *PPACKAGE_FILES;

static HRESULT
CheckLocalRequest(PCWSTR pszServer, PCWSTR pszEnvironment)
{
    WCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD Size = ARRAYSIZE(ComputerName);

    if (pszEnvironment && *pszEnvironment && _wcsicmp(pszEnvironment, wszCurrentEnvironment) != 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_ENVIRONMENT);

    if (!pszServer || !*pszServer)
        return S_OK;

    while (*pszServer == L'\\')
        pszServer++;

    if (GetComputerNameW(ComputerName, &Size) && _wcsicmp(pszServer, ComputerName) == 0)
        return S_OK;

    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

static WORD
GetNativeArchitecture(VOID)
{
    SYSTEM_INFO SystemInfo;

    GetNativeSystemInfo(&SystemInfo);
    return SystemInfo.wProcessorArchitecture;
}

static HRESULT
ReturnString(PCWSTR Source, PWSTR Destination, PULONG pcchDestination)
{
    ULONG Needed = (ULONG)wcslen(Source) + 1;

    if (!Destination || *pcchDestination < Needed)
    {
        *pcchDestination = Needed;
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    StringCchCopyW(Destination, *pcchDestination, Source);
    *pcchDestination = Needed;
    return S_OK;
}

HRESULT WINAPI
UploadPrinterDriverPackageW(LPCWSTR pszServer, LPCWSTR pszInfPath, LPCWSTR pszEnvironment, DWORD dwFlags,
                            HWND hwnd, LPWSTR pszDestInfPath, PULONG pcchDestInfPath)
{
    WCHAR StoreInf[MAX_PATH];
    DWORD Length = ARRAYSIZE(StoreInf);
    HRESULT hr;

    TRACE("UploadPrinterDriverPackageW(%S, %S, %S, 0x%lx, %p, %p, %p)\n",
          pszServer, pszInfPath, pszEnvironment, dwFlags, hwnd, pszDestInfPath, pcchDestInfPath);

    UNREFERENCED_PARAMETER(hwnd);

    if (!pszInfPath || !pcchDestInfPath ||
        (dwFlags & ~(UPDP_SILENT_UPLOAD | UPDP_UPLOAD_ALWAYS | UPDP_CHECK_DRIVERSTORE)))
    {
        return E_INVALIDARG;
    }

    hr = CheckLocalRequest(pszServer, pszEnvironment);
    if (FAILED(hr))
        return hr;

    if ((dwFlags & UPDP_UPLOAD_ALWAYS) &&
        !SetupCopyOEMInfW(pszInfPath, NULL, SPOST_NONE, 0, NULL, 0, NULL, NULL))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    hr = DriverStoreAddDriverPackageW(pszInfPath, NULL, NULL, GetNativeArchitecture(), StoreInf, &Length);
    if (FAILED(hr))
        return hr;

    return ReturnString(StoreInf, pszDestInfPath, pcchDestInfPath);
}

static HRESULT
IsDriverPackageInUse(PCWSTR InfPath)
{
    WCHAR Key[MAX_PATH], Name[MAX_PATH], Value[MAX_PATH];
    HRESULT hr = S_FALSE;
    HKEY hDrivers;
    DWORD i, Size;
    LSTATUS Error;

    if (FAILED(StringCchPrintfW(Key, ARRAYSIZE(Key), DRIVERS_KEY, wszCurrentEnvironment)))
        return E_FAIL;
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Key, 0, KEY_READ, &hDrivers);
    if (Error == ERROR_FILE_NOT_FOUND)
        return S_FALSE;
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);

    for (i = 0; hr == S_FALSE; i++)
    {
        Size = ARRAYSIZE(Name);
        Error = RegEnumKeyExW(hDrivers, i, Name, &Size, NULL, NULL, NULL, NULL);
        if (Error == ERROR_NO_MORE_ITEMS)
            break;
        if (Error != ERROR_SUCCESS)
        {
            hr = HRESULT_FROM_WIN32(Error);
            break;
        }
        Size = sizeof(Value);
        Error = RegGetValueW(hDrivers, Name, L"InfPath", RRF_RT_REG_SZ, NULL, Value, &Size);
        if (Error == ERROR_FILE_NOT_FOUND)
            continue;
        if (Error != ERROR_SUCCESS)
            hr = HRESULT_FROM_WIN32(Error);
        else if (_wcsicmp(Value, InfPath) == 0)
            hr = S_OK;
    }

    RegCloseKey(hDrivers);
    return hr;
}

HRESULT WINAPI
DeletePrinterDriverPackageW(LPCWSTR pszServer, LPCWSTR pszInfPath, LPCWSTR pszEnvironment)
{
    HRESULT hr;

    TRACE("DeletePrinterDriverPackageW(%S, %S, %S)\n", pszServer, pszInfPath, pszEnvironment);

    if (!pszInfPath)
        return E_INVALIDARG;

    hr = CheckLocalRequest(pszServer, pszEnvironment);
    if (FAILED(hr))
        return hr;

    hr = IsDriverPackageInUse(pszInfPath);
    if (FAILED(hr))
        return hr;
    if (hr == S_OK)
        return HRESULT_FROM_WIN32(ERROR_PRINTER_DRIVER_PACKAGE_IN_USE);

    return DriverStoreDeleteDriverPackageW(pszInfPath, NULL, NULL);
}

static BOOL
AddPackageFile(PPACKAGE_FILES Files, PCWSTR Name, PCWSTR Source)
{
    DWORD i;

    if (!Name || !*Name)
        return TRUE;

    for (i = 0; i < Files->Count; i++)
    {
        if (_wcsicmp(Files->Names[i], Name) == 0)
            return TRUE;
    }

    if (Files->Count == MAX_PACKAGE_FILES)
        return FALSE;

    Files->Names[Files->Count] = AllocSplStr(Name);
    Files->Sources[Files->Count] = NULL;
    if (!Files->Names[Files->Count])
        return FALSE;
    if (Source && *Source && _wcsicmp(Source, Name) != 0)
    {
        Files->Sources[Files->Count] = AllocSplStr(Source);
        if (!Files->Sources[Files->Count])
        {
            DllFreeSplStr(Files->Names[Files->Count]);
            return FALSE;
        }
    }
    Files->Count++;
    return TRUE;
}

static VOID
FreePackageFiles(PPACKAGE_FILES Files)
{
    while (Files->Count)
    {
        Files->Count--;
        DllFreeSplStr(Files->Names[Files->Count]);
        if (Files->Sources[Files->Count])
            DllFreeSplStr(Files->Sources[Files->Count]);
    }
}

static BOOL
CollectCopyFiles(HINF hInf, PCWSTR Section, PPACKAGE_FILES Files)
{
    WCHAR Entry[MAX_PATH], Name[MAX_PATH], Source[MAX_PATH];
    INFCONTEXT Context, FileContext;
    DWORD Field, FieldCount;

    if (!SetupFindFirstLineW(hInf, Section, L"CopyFiles", &Context))
        return TRUE;

    do
    {
        FieldCount = SetupGetFieldCount(&Context);
        for (Field = 1; Field <= FieldCount; Field++)
        {
            if (!SetupGetStringFieldW(&Context, Field, Entry, ARRAYSIZE(Entry), NULL) || !*Entry)
                continue;

            if (Entry[0] == L'@')
            {
                if (!AddPackageFile(Files, Entry + 1, NULL))
                    return FALSE;
                continue;
            }

            if (SetupGetLineCountW(hInf, Entry) < 0)
            {
                ERR("CopyFiles section %S not found\n", Entry);
                SetLastError(ERROR_SECTION_NOT_FOUND);
                return FALSE;
            }
            if (!SetupFindFirstLineW(hInf, Entry, NULL, &FileContext))
                continue;
            do
            {
                if (!SetupGetStringFieldW(&FileContext, 2, Source, ARRAYSIZE(Source), NULL))
                    *Source = UNICODE_NULL;
                if (SetupGetStringFieldW(&FileContext, 1, Name, ARRAYSIZE(Name), NULL) &&
                    !AddPackageFile(Files, Name, Source))
                {
                    return FALSE;
                }
            } while (SetupFindNextLine(&FileContext, &FileContext));
        }
    } while (SetupFindNextMatchLineW(&Context, L"CopyFiles", &Context));

    return TRUE;
}

static BOOL
GetDirective(HINF hInf, PCWSTR *Sections, DWORD SectionCount, PCWSTR Key, PWSTR Value, DWORD ValueSize)
{
    WCHAR DataSection[MAX_PATH];
    INFCONTEXT Context;
    DWORD i;

    for (i = 0; i < SectionCount; i++)
    {
        if (SetupFindFirstLineW(hInf, Sections[i], Key, &Context) &&
            SetupGetStringFieldW(&Context, 1, Value, ValueSize, NULL))
        {
            return TRUE;
        }
    }

    for (i = 0; i < SectionCount; i++)
    {
        if (SetupFindFirstLineW(hInf, Sections[i], L"DataSection", &Context) &&
            SetupGetStringFieldW(&Context, 1, DataSection, ARRAYSIZE(DataSection), NULL) &&
            SetupFindFirstLineW(hInf, DataSection, Key, &Context) &&
            SetupGetStringFieldW(&Context, 1, Value, ValueSize, NULL))
        {
            return TRUE;
        }
    }

    *Value = UNICODE_NULL;
    return FALSE;
}

static BOOL
FindModelSection(HINF hInf, PCWSTR DriverName, PWSTR Section, DWORD SectionSize)
{
    WCHAR Models[MAX_PATH];
    INFCONTEXT MfgContext, ModelContext;

    if (!SetupFindFirstLineW(hInf, L"Manufacturer", NULL, &MfgContext))
        return FALSE;

    do
    {
        if (!SetupDiGetActualModelsSectionW(&MfgContext, NULL, Models, ARRAYSIZE(Models), NULL, NULL))
            continue;

        if (SetupFindFirstLineW(hInf, Models, DriverName, &ModelContext) &&
            SetupGetStringFieldW(&ModelContext, 1, Section, SectionSize, NULL))
        {
            return TRUE;
        }
    } while (SetupFindNextLine(&MfgContext, &MfgContext));

    return FALSE;
}

static BOOL
LocatePackageFile(HINF hInf, PCWSTR PackageDir, PCWSTR DriverDir, PCWSTR Name, PWSTR Path, DWORD PathSize)
{
    WCHAR SourcePath[MAX_PATH] = L"", SubDir[MAX_PATH] = L"";
    UINT SourceId;

    if (SetupGetSourceFileLocationW(hInf, NULL, Name, &SourceId, SubDir, ARRAYSIZE(SubDir), NULL))
        SetupGetSourceInfoW(hInf, SourceId, SRCINFO_PATH, SourcePath, ARRAYSIZE(SourcePath), NULL);

    if (SUCCEEDED(StringCchPrintfW(Path, PathSize, L"%s%s%s%s%s\\%s", PackageDir,
                                   *SourcePath ? L"\\" : L"", SourcePath,
                                   *SubDir ? L"\\" : L"", SubDir, Name)) &&
        GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES)
    {
        return TRUE;
    }

    if (SUCCEEDED(StringCchPrintfW(Path, PathSize, L"%s\\3\\%s", DriverDir, Name)) &&
        GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES)
    {
        return TRUE;
    }

    ERR("Printer driver file %S not found\n", Name);
    return FALSE;
}

static BOOL
ResolvePackageFile(HINF hInf, PPACKAGE_FILES Files, PCWSTR PackageDir, PCWSTR DriverDir, PCWSTR Name,
                   PWSTR StagingDir, PPACKAGE_FILES Staged, PWSTR Path, DWORD PathSize)
{
    WCHAR Copy[MAX_PATH];
    PCWSTR Source = Name;
    DWORD i, Attempt;

    for (i = 0; i < Files->Count; i++)
    {
        if (Files->Sources[i] && _wcsicmp(Files->Names[i], Name) == 0)
            Source = Files->Sources[i];
    }

    if (!LocatePackageFile(hInf, PackageDir, DriverDir, Source, Path, PathSize))
        return FALSE;
    if (Source == Name)
        return TRUE;

    for (Attempt = 0; !*StagingDir && Attempt < 100; Attempt++)
    {
        if (FAILED(StringCchPrintfW(StagingDir, MAX_PATH, L"%s\\pkgstage-%lx-%lx", DriverDir,
                                    GetCurrentProcessId(), GetTickCount() + Attempt)))
        {
            *StagingDir = UNICODE_NULL;
            break;
        }
        if (!CreateDirectoryW(StagingDir, NULL))
            *StagingDir = UNICODE_NULL;
    }
    if (!*StagingDir || FAILED(StringCchPrintfW(Copy, ARRAYSIZE(Copy), L"%s\\%s", StagingDir, Name)))
    {
        ERR("No staging directory for %S (%lu)\n", Name, GetLastError());
        return FALSE;
    }

    for (i = 0; i < Staged->Count && _wcsicmp(Staged->Names[i], Copy) != 0; i++);
    if (i == Staged->Count)
    {
        if (!CopyFileW(Path, Copy, TRUE))
        {
            ERR("Cannot copy %S as %S (%lu)\n", Source, Name, GetLastError());
            return FALSE;
        }
        if (!AddPackageFile(Staged, Copy, NULL))
        {
            DeleteFileW(Copy);
            return FALSE;
        }
    }
    return SUCCEEDED(StringCchCopyW(Path, PathSize, Copy));
}

HRESULT WINAPI
InstallPrinterDriverFromPackageW(LPCWSTR pszServer, LPCWSTR pszInfPath, LPCWSTR pszDriverName,
                                 LPCWSTR pszEnvironment, DWORD dwFlags)
{
    WCHAR InstallSection[MAX_PATH], ActualSection[MAX_PATH], NeedsSection[MAX_PATH], Include[MAX_PATH];
    WCHAR DriverFile[MAX_PATH], ConfigFile[MAX_PATH], DataFile[MAX_PATH], HelpFile[MAX_PATH];
    WCHAR DefaultDataType[MAX_PATH], PackageDir[MAX_PATH], DriverDir[MAX_PATH];
    WCHAR DriverPath[MAX_PATH], ConfigPath[MAX_PATH], DataPath[MAX_PATH], HelpPath[MAX_PATH], FilePath[MAX_PATH];
    PCWSTR Sections[16];
    DWORD SectionCount = 0, Field, FieldCount, Size, i, j, DependentLength = 1;
    PACKAGE_FILES Files = { 0 }, Staged = { 0 };
    WCHAR StagingDir[MAX_PATH] = L"";
    PWSTR DependentFiles = NULL, Next, Slash;
    DRIVER_INFO_8W DriverInfo = { 0 };
    INFCONTEXT Context;
    HINF hInf;
    HRESULT hr;

    TRACE("InstallPrinterDriverFromPackageW(%S, %S, %S, %S, 0x%lx)\n",
          pszServer, pszInfPath, pszDriverName, pszEnvironment, dwFlags);

    if (!pszDriverName || !*pszDriverName || (dwFlags & ~IPDFP_COPY_ALL_FILES))
        return E_INVALIDARG;

    if (!pszInfPath)
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    hr = CheckLocalRequest(pszServer, pszEnvironment);
    if (FAILED(hr))
        return hr;

    if (FAILED(StringCchCopyW(PackageDir, ARRAYSIZE(PackageDir), pszInfPath)) ||
        !(Slash = wcsrchr(PackageDir, L'\\')))
    {
        return E_INVALIDARG;
    }
    *Slash = UNICODE_NULL;

    Size = sizeof(DriverDir);
    if (!GetPrinterDriverDirectoryW(NULL, (PWSTR)wszCurrentEnvironment, 1, (PBYTE)DriverDir, Size, &Size))
        return HRESULT_FROM_WIN32(GetLastError());

    hInf = SetupOpenInfFileW(pszInfPath, NULL, INF_STYLE_WIN4, NULL);
    if (hInf == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());

    if (!FindModelSection(hInf, pszDriverName, InstallSection, ARRAYSIZE(InstallSection)) ||
        !SetupDiGetActualSectionToInstallW(hInf, InstallSection, ActualSection, ARRAYSIZE(ActualSection),
                                           NULL, NULL))
    {
        hr = HRESULT_FROM_WIN32(ERROR_UNKNOWN_PRINTER_DRIVER);
        goto Cleanup;
    }
    Sections[SectionCount++] = ActualSection;

    for (i = 0; i < SectionCount; i++)
    {
        if (SetupFindFirstLineW(hInf, Sections[i], L"Include", &Context))
        {
            FieldCount = SetupGetFieldCount(&Context);
            for (Field = 1; Field <= FieldCount; Field++)
            {
                if (SetupGetStringFieldW(&Context, Field, Include, ARRAYSIZE(Include), NULL) &&
                    !SetupOpenAppendInfFileW(Include, hInf, NULL))
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    goto Cleanup;
                }
            }
        }

        if (!SetupFindFirstLineW(hInf, Sections[i], L"Needs", &Context))
            continue;
        FieldCount = SetupGetFieldCount(&Context);
        for (Field = 1; Field <= FieldCount; Field++)
        {
            if (!SetupGetStringFieldW(&Context, Field, NeedsSection, ARRAYSIZE(NeedsSection), NULL) ||
                !*NeedsSection)
            {
                continue;
            }
            for (j = 0; j < SectionCount && _wcsicmp(Sections[j], NeedsSection) != 0; j++);
            if (j < SectionCount)
                continue;

            if (SetupGetLineCountW(hInf, NeedsSection) < 0)
            {
                ERR("Needed section %S not found\n", NeedsSection);
                hr = HRESULT_FROM_WIN32(ERROR_SECTION_NOT_FOUND);
                goto Cleanup;
            }
            if (SectionCount == ARRAYSIZE(Sections))
            {
                hr = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
                goto Cleanup;
            }
            Sections[SectionCount] = AllocSplStr(NeedsSection);
            if (!Sections[SectionCount])
            {
                hr = E_OUTOFMEMORY;
                goto Cleanup;
            }
            SectionCount++;
        }
    }

    if (SetupFindFirstLineW(hInf, L"Version", L"ClassVer", &Context) &&
        SetupGetStringFieldW(&Context, 1, Include, ARRAYSIZE(Include), NULL) && Include[0] == L'4')
    {
        hr = HRESULT_FROM_WIN32(ERROR_UNKNOWN_PRINTER_DRIVER);
        goto Cleanup;
    }

    if (!GetDirective(hInf, Sections, SectionCount, L"DriverFile", DriverFile, ARRAYSIZE(DriverFile)))
        StringCchCopyW(DriverFile, ARRAYSIZE(DriverFile), InstallSection);
    if (!GetDirective(hInf, Sections, SectionCount, L"ConfigFile", ConfigFile, ARRAYSIZE(ConfigFile)))
        StringCchCopyW(ConfigFile, ARRAYSIZE(ConfigFile), DriverFile);
    GetDirective(hInf, Sections, SectionCount, L"HelpFile", HelpFile, ARRAYSIZE(HelpFile));
    GetDirective(hInf, Sections, SectionCount, L"DefaultDataType", DefaultDataType, ARRAYSIZE(DefaultDataType));
    if (!GetDirective(hInf, Sections, SectionCount, L"DataFile", DataFile, ARRAYSIZE(DataFile)))
        StringCchCopyW(DataFile, ARRAYSIZE(DataFile), InstallSection);

    /* TODO: LanguageMonitor and VendorSetup are not processed */

    for (i = 0; i < SectionCount; i++)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        if (!CollectCopyFiles(hInf, Sections[i], &Files))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            goto Cleanup;
        }
    }

    if (!ResolvePackageFile(hInf, &Files, PackageDir, DriverDir, DriverFile, StagingDir, &Staged, DriverPath, ARRAYSIZE(DriverPath)) ||
        !ResolvePackageFile(hInf, &Files, PackageDir, DriverDir, ConfigFile, StagingDir, &Staged, ConfigPath, ARRAYSIZE(ConfigPath)) ||
        !ResolvePackageFile(hInf, &Files, PackageDir, DriverDir, DataFile, StagingDir, &Staged, DataPath, ARRAYSIZE(DataPath)) ||
        (*HelpFile &&
         !ResolvePackageFile(hInf, &Files, PackageDir, DriverDir, HelpFile, StagingDir, &Staged, HelpPath, ARRAYSIZE(HelpPath))))
    {
        hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        goto Cleanup;
    }

    for (i = 0; i < Files.Count; i++)
    {
        if (!ResolvePackageFile(hInf, &Files, PackageDir, DriverDir, Files.Names[i], StagingDir, &Staged,
                                FilePath, ARRAYSIZE(FilePath)))
        {
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            goto Cleanup;
        }
        DependentLength += (DWORD)wcslen(FilePath) + 1;
    }

    DependentFiles = DllAllocSplMem(DependentLength * sizeof(WCHAR));
    if (!DependentFiles)
    {
        hr = E_OUTOFMEMORY;
        goto Cleanup;
    }

    Next = DependentFiles;
    for (i = 0; i < Files.Count; i++)
    {
        if (!_wcsicmp(Files.Names[i], DriverFile) || !_wcsicmp(Files.Names[i], ConfigFile) ||
            !_wcsicmp(Files.Names[i], DataFile) || !_wcsicmp(Files.Names[i], HelpFile))
        {
            continue;
        }

        if (!ResolvePackageFile(hInf, &Files, PackageDir, DriverDir, Files.Names[i], StagingDir, &Staged,
                                FilePath, ARRAYSIZE(FilePath)) ||
            FAILED(StringCchCopyW(Next, DependentLength - (DWORD)(Next - DependentFiles), FilePath)))
        {
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            goto Cleanup;
        }
        Next += wcslen(Next) + 1;
    }
    *Next = UNICODE_NULL;

    DriverInfo.cVersion = 3;
    DriverInfo.pName = (PWSTR)pszDriverName;
    DriverInfo.pEnvironment = (PWSTR)wszCurrentEnvironment;
    DriverInfo.pDriverPath = DriverPath;
    DriverInfo.pConfigFile = ConfigPath;
    DriverInfo.pDataFile = DataPath;
    DriverInfo.pHelpFile = *HelpFile ? HelpPath : NULL;
    DriverInfo.pDependentFiles = DependentFiles;
    DriverInfo.pDefaultDataType = *DefaultDataType ? DefaultDataType : NULL;
    DriverInfo.pszInfPath = (PWSTR)pszInfPath;

    if (!AddPrinterDriverExW(NULL, 8, (PBYTE)&DriverInfo,
                             APD_COPY_FROM_DIRECTORY |
                             ((dwFlags & IPDFP_COPY_ALL_FILES) ? APD_COPY_ALL_FILES : APD_COPY_NEW_FILES)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }

    hr = S_OK;

Cleanup:
    if (DependentFiles)
        DllFreeSplMem(DependentFiles);
    for (i = 0; i < Staged.Count; i++)
        DeleteFileW(Staged.Names[i]);
    if (*StagingDir)
        RemoveDirectoryW(StagingDir);
    FreePackageFiles(&Staged);
    FreePackageFiles(&Files);
    for (i = 1; i < SectionCount; i++)
        DllFreeSplStr((PWSTR)Sections[i]);
    SetupCloseInfFile(hInf);
    return hr;
}

static PWSTR
AnsiToHeapUnicode(PCSTR Ansi, HRESULT *phr)
{
    PWSTR Unicode;
    INT Length;

    if (!Ansi)
        return NULL;

    Length = MultiByteToWideChar(CP_ACP, 0, Ansi, -1, NULL, 0);
    Unicode = Length ? DllAllocSplMem(Length * sizeof(WCHAR)) : NULL;
    if (!Unicode)
    {
        *phr = E_OUTOFMEMORY;
        return NULL;
    }

    MultiByteToWideChar(CP_ACP, 0, Ansi, -1, Unicode, Length);
    return Unicode;
}

HRESULT WINAPI
UploadPrinterDriverPackageA(LPCSTR pszServer, LPCSTR pszInfPath, LPCSTR pszEnvironment, DWORD dwFlags,
                            HWND hwnd, LPSTR pszDestInfPath, PULONG pcchDestInfPath)
{
    PWSTR ServerW, InfPathW, EnvironmentW;
    WCHAR DestW[MAX_PATH];
    ULONG DestLength = ARRAYSIZE(DestW);
    HRESULT hr = S_OK;
    INT Needed;

    if (!pcchDestInfPath)
        return E_INVALIDARG;

    ServerW = AnsiToHeapUnicode(pszServer, &hr);
    InfPathW = AnsiToHeapUnicode(pszInfPath, &hr);
    EnvironmentW = AnsiToHeapUnicode(pszEnvironment, &hr);
    if (SUCCEEDED(hr))
        hr = UploadPrinterDriverPackageW(ServerW, InfPathW, EnvironmentW, dwFlags, hwnd, DestW, &DestLength);

    if (SUCCEEDED(hr))
    {
        Needed = WideCharToMultiByte(CP_ACP, 0, DestW, -1, NULL, 0, NULL, NULL);
        if (!pszDestInfPath || *pcchDestInfPath < (ULONG)Needed)
            hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        else
            WideCharToMultiByte(CP_ACP, 0, DestW, -1, pszDestInfPath, Needed, NULL, NULL);
        *pcchDestInfPath = Needed;
    }

    DllFreeSplMem(ServerW);
    DllFreeSplMem(InfPathW);
    DllFreeSplMem(EnvironmentW);
    return hr;
}

HRESULT WINAPI
DeletePrinterDriverPackageA(LPCSTR pszServer, LPCSTR pszInfPath, LPCSTR pszEnvironment)
{
    PWSTR ServerW, InfPathW, EnvironmentW;
    HRESULT hr = S_OK;

    ServerW = AnsiToHeapUnicode(pszServer, &hr);
    InfPathW = AnsiToHeapUnicode(pszInfPath, &hr);
    EnvironmentW = AnsiToHeapUnicode(pszEnvironment, &hr);
    if (SUCCEEDED(hr))
        hr = DeletePrinterDriverPackageW(ServerW, InfPathW, EnvironmentW);

    DllFreeSplMem(ServerW);
    DllFreeSplMem(InfPathW);
    DllFreeSplMem(EnvironmentW);
    return hr;
}

HRESULT WINAPI
InstallPrinterDriverFromPackageA(LPCSTR pszServer, LPCSTR pszInfPath, LPCSTR pszDriverName,
                                 LPCSTR pszEnvironment, DWORD dwFlags)
{
    PWSTR ServerW, InfPathW, DriverNameW, EnvironmentW;
    HRESULT hr = S_OK;

    ServerW = AnsiToHeapUnicode(pszServer, &hr);
    InfPathW = AnsiToHeapUnicode(pszInfPath, &hr);
    DriverNameW = AnsiToHeapUnicode(pszDriverName, &hr);
    EnvironmentW = AnsiToHeapUnicode(pszEnvironment, &hr);
    if (SUCCEEDED(hr))
        hr = InstallPrinterDriverFromPackageW(ServerW, InfPathW, DriverNameW, EnvironmentW, dwFlags);

    DllFreeSplMem(ServerW);
    DllFreeSplMem(InfPathW);
    DllFreeSplMem(DriverNameW);
    DllFreeSplMem(EnvironmentW);
    return hr;
}
