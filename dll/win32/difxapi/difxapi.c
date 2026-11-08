/*
 * PROJECT:     LiberNT Driver Install Frameworks API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Preinstall, install, uninstall and locate PnP function driver packages
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdio.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winnls.h>
#include <winuser.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <newdev.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>
#include <regstr.h>
#include <strsafe.h>
#include <difxapi.h>

#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(difxapi);

HRESULT WINAPI DriverStoreFindDriverPackageW(PCWSTR, PVOID, PVOID, WORD, PVOID, PWSTR, PDWORD);

#define DIFX_ID_LIST_SIZE 0x8000

typedef struct _DIFX_PACKAGE
{
    WCHAR InfPath[MAX_PATH];
    WCHAR Directory[MAX_PATH];
    HINF Inf;
} DIFX_PACKAGE, *PDIFX_PACKAGE;

typedef struct _DIFX_LOGGER
{
    DIFXLOGCALLBACK_W WinApiW;
    DIFXLOGCALLBACK_A WinApiA;
    PVOID WinApiContext;
    DIFXAPILOGCALLBACK_W CdeclW;
    DIFXAPILOGCALLBACK_A CdeclA;
    PVOID CdeclContext;
} DIFX_LOGGER;

static CRITICAL_SECTION DifxLogLock;
static DIFX_LOGGER DifxLogger;

static
VOID
DifxLog(DIFXAPI_LOG Event, DWORD Error, PCWSTR Format, ...)
{
    WCHAR TextW[512];
    CHAR TextA[512];
    DIFX_LOGGER Logger;
    va_list Args;

    EnterCriticalSection(&DifxLogLock);
    Logger = DifxLogger;
    LeaveCriticalSection(&DifxLogLock);

    if (!Logger.WinApiW && !Logger.WinApiA && !Logger.CdeclW && !Logger.CdeclA)
        return;

    va_start(Args, Format);
    StringCchVPrintfW(TextW, ARRAYSIZE(TextW), Format, Args);
    va_end(Args);
    WideCharToMultiByte(CP_ACP, 0, TextW, -1, TextA, sizeof(TextA), NULL, NULL);

    if (Logger.WinApiW)
        Logger.WinApiW(Event, Error, TextW, Logger.WinApiContext);
    else if (Logger.WinApiA)
        Logger.WinApiA(Event, Error, TextA, Logger.WinApiContext);

    if (Logger.CdeclW)
        Logger.CdeclW(Event, Error, TextW, Logger.CdeclContext);
    else if (Logger.CdeclA)
        Logger.CdeclA(Event, Error, TextA, Logger.CdeclContext);
}

static
DWORD
DifxResult(PCWSTR Operation, PCWSTR InfPath, DWORD Error)
{
    if (Error == ERROR_SUCCESS)
        DifxLog(DIFXAPI_SUCCESS, Error, L"%ls of '%ls' succeeded", Operation, InfPath ? InfPath : L"");
    else
        DifxLog(DIFXAPI_ERROR, Error, L"%ls of '%ls' failed with 0x%08lx", Operation, InfPath ? InfPath : L"", Error);
    return Error;
}

static
BOOL
DifxIsAdministrator(VOID)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    PSID AdministratorsGroup;
    BOOL IsMember = FALSE;

    if (!AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &AdministratorsGroup))
    {
        return FALSE;
    }

    if (!CheckTokenMembership(NULL, AdministratorsGroup, &IsMember))
        IsMember = FALSE;

    FreeSid(AdministratorsGroup);
    return IsMember;
}

static
DWORD
DifxCheckCaller(VOID)
{
    BOOL Wow64 = FALSE;

    if (IsWow64Process(GetCurrentProcess(), &Wow64) && Wow64)
        return ERROR_IN_WOW64;

    return ERROR_SUCCESS;
}

static
BOOL
DifxIsFullyQualified(PCWSTR Path)
{
    if (Path[0] == L'\\' && Path[1] == L'\\')
        return Path[2] != UNICODE_NULL;

    return ((Path[0] >= L'A' && Path[0] <= L'Z') || (Path[0] >= L'a' && Path[0] <= L'z')) &&
           Path[1] == L':' && (Path[2] == L'\\' || Path[2] == L'/');
}

static
BOOL
DifxIsSystemInfDirectory(PCWSTR Directory)
{
    WCHAR InfDirectory[MAX_PATH];
    UINT Length;

    Length = GetSystemWindowsDirectoryW(InfDirectory, ARRAYSIZE(InfDirectory));
    if (!Length || Length >= ARRAYSIZE(InfDirectory) ||
        FAILED(StringCchCatW(InfDirectory, ARRAYSIZE(InfDirectory), L"\\INF")))
    {
        return FALSE;
    }

    return !_wcsicmp(Directory, InfDirectory);
}

static
DWORD
DifxResolvePackagePath(PCWSTR InfPath, PDIFX_PACKAGE Package)
{
    PWSTR FilePart = NULL;
    DWORD Length, Attributes;

    if (!InfPath)
        return ERROR_INVALID_PARAMETER;
    if (wcslen(InfPath) >= MAX_PATH)
        return ERROR_FILENAME_EXCED_RANGE;
    if (!DifxIsFullyQualified(InfPath))
        return ERROR_INVALID_NAME;

    Length = GetFullPathNameW(InfPath, ARRAYSIZE(Package->InfPath), Package->InfPath, &FilePart);
    if (!Length || Length >= ARRAYSIZE(Package->InfPath) || !FilePart || FilePart == Package->InfPath)
        return ERROR_INVALID_NAME;

    Attributes = GetFileAttributesW(Package->InfPath);
    if (Attributes == INVALID_FILE_ATTRIBUTES || (Attributes & FILE_ATTRIBUTE_DIRECTORY))
        return ERROR_FILE_NOT_FOUND;

    StringCchCopyNW(Package->Directory, ARRAYSIZE(Package->Directory), Package->InfPath, FilePart - Package->InfPath - 1);
    if (DifxIsSystemInfDirectory(Package->Directory))
        return ERROR_CANT_ACCESS_FILE;

    return ERROR_SUCCESS;
}

static
DWORD
DifxOpenPackage(PCWSTR InfPath, PDIFX_PACKAGE Package)
{
    DWORD Error;

    Package->Inf = INVALID_HANDLE_VALUE;
    Error = DifxResolvePackagePath(InfPath, Package);
    if (Error != ERROR_SUCCESS)
        return Error;

    if (!DifxIsAdministrator())
        return ERROR_ACCESS_DENIED;

    Package->Inf = SetupOpenInfFileW(Package->InfPath, NULL, INF_STYLE_WIN4, NULL);
    if (Package->Inf == INVALID_HANDLE_VALUE)
        return GetLastError();

    return ERROR_SUCCESS;
}

static
VOID
DifxClosePackage(PDIFX_PACKAGE Package)
{
    if (Package->Inf != INVALID_HANDLE_VALUE)
        SetupCloseInfFile(Package->Inf);
    Package->Inf = INVALID_HANDLE_VALUE;
}

static
BOOL
DifxIsFunctionDriverPackage(PDIFX_PACKAGE Package)
{
    INFCONTEXT Context;

    return SetupFindFirstLineW(Package->Inf, L"Manufacturer", NULL, &Context);
}

static
PCWSTR
DifxPlatformSuffix(VOID)
{
    SYSTEM_INFO SystemInfo;

    GetNativeSystemInfo(&SystemInfo);
    switch (SystemInfo.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_INTEL: return L"x86";
        case PROCESSOR_ARCHITECTURE_AMD64: return L"amd64";
        case PROCESSOR_ARCHITECTURE_ARM: return L"arm";
        case PROCESSOR_ARCHITECTURE_ARM64: return L"arm64";
        case PROCESSOR_ARCHITECTURE_IA64: return L"ia64";
        default: return NULL;
    }
}

static
BOOL
DifxGetCatalogName(PDIFX_PACKAGE Package, PWSTR Catalog, DWORD CatalogSize)
{
    WCHAR Key[32];
    PCWSTR Platform = DifxPlatformSuffix();

    if (Platform && SUCCEEDED(StringCchPrintfW(Key, ARRAYSIZE(Key), L"CatalogFile.NT%ls", Platform)) &&
        SetupGetLineTextW(NULL, Package->Inf, L"Version", Key, Catalog, CatalogSize, NULL) && Catalog[0])
    {
        return TRUE;
    }

    if (SetupGetLineTextW(NULL, Package->Inf, L"Version", L"CatalogFile.NT", Catalog, CatalogSize, NULL) && Catalog[0])
        return TRUE;

    return SetupGetLineTextW(NULL, Package->Inf, L"Version", L"CatalogFile", Catalog, CatalogSize, NULL) && Catalog[0];
}

static
DWORD
DifxVerifyCatalogSignature(PCWSTR CatalogPath)
{
    GUID Action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO FileInfo;
    WINTRUST_DATA TrustData;
    LONG Result;

    ZeroMemory(&FileInfo, sizeof(FileInfo));
    FileInfo.cbStruct = sizeof(FileInfo);
    FileInfo.pcwszFilePath = CatalogPath;

    ZeroMemory(&TrustData, sizeof(TrustData));
    TrustData.cbStruct = sizeof(TrustData);
    TrustData.dwUIChoice = WTD_UI_NONE;
    TrustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    TrustData.dwUnionChoice = WTD_CHOICE_FILE;
    TrustData.pFile = &FileInfo;
    TrustData.dwStateAction = WTD_STATEACTION_VERIFY;

    Result = WinVerifyTrust(INVALID_HANDLE_VALUE, &Action, &TrustData);

    TrustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &Action, &TrustData);

    return (DWORD)Result;
}

static
BOOL
DifxCatalogHasMember(HANDLE Catalog, HANDLE File, PCWSTR HashAlgorithm)
{
    HCATADMIN Admin;
    BYTE Hash[64];
    WCHAR Tag[ARRAYSIZE(Hash) * 2 + 1];
    DWORD HashSize = sizeof(Hash), Index;
    BOOL Found = FALSE;

    if (!CryptCATAdminAcquireContext2(&Admin, NULL, HashAlgorithm, NULL, 0))
        return FALSE;

    SetFilePointer(File, 0, NULL, FILE_BEGIN);
    if (CryptCATAdminCalcHashFromFileHandle2(Admin, File, &HashSize, Hash, 0) && HashSize <= sizeof(Hash))
    {
        for (Index = 0; Index < HashSize; Index++)
            StringCchPrintfW(&Tag[Index * 2], 3, L"%02X", Hash[Index]);
        Found = CryptCATGetMemberInfo(Catalog, Tag) != NULL;
    }

    CryptCATAdminReleaseContext(Admin, 0);
    return Found;
}

static
BOOL
DifxIsCatalogFile(PCWSTR CatalogPath)
{
    PCCTL_CONTEXT Ctl = NULL;

    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, CatalogPath, CERT_QUERY_CONTENT_FLAG_CTL, CERT_QUERY_FORMAT_FLAG_BINARY,
                          0, NULL, NULL, NULL, NULL, NULL, (const void **)&Ctl))
    {
        return FALSE;
    }

    CertFreeCTLContext(Ctl);
    return TRUE;
}

static
DWORD
DifxVerifyPackageSignature(PDIFX_PACKAGE Package)
{
    WCHAR CatalogName[MAX_PATH], CatalogPath[MAX_PATH];
    HANDLE Catalog, InfFile;
    DWORD Error;
    BOOL Member;

    if (!DifxGetCatalogName(Package, CatalogName, ARRAYSIZE(CatalogName)))
        return (DWORD)TRUST_E_NOSIGNATURE;

    if (FAILED(StringCchPrintfW(CatalogPath, ARRAYSIZE(CatalogPath), L"%ls\\%ls", Package->Directory, CatalogName)))
        return ERROR_FILENAME_EXCED_RANGE;
    if (GetFileAttributesW(CatalogPath) == INVALID_FILE_ATTRIBUTES)
        return (DWORD)CRYPT_E_FILE_ERROR;
    if (!DifxIsCatalogFile(CatalogPath))
        return ERROR_INVALID_CATALOG_DATA;

    Catalog = CryptCATOpen(CatalogPath, CRYPTCAT_OPEN_EXISTING, 0, 0, 0);
    if (Catalog == INVALID_HANDLE_VALUE)
        return ERROR_INVALID_CATALOG_DATA;

    InfFile = CreateFileW(Package->InfPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (InfFile == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
        CryptCATClose(Catalog);
        return Error;
    }

    Member = DifxCatalogHasMember(Catalog, InfFile, BCRYPT_SHA256_ALGORITHM) ||
             DifxCatalogHasMember(Catalog, InfFile, BCRYPT_SHA1_ALGORITHM);
    CloseHandle(InfFile);
    CryptCATClose(Catalog);
    if (!Member)
        return ERROR_FILE_HASH_NOT_IN_CATALOG;

    return DifxVerifyCatalogSignature(CatalogPath);
}

static
BOOL
DifxGetInstallSection(PDIFX_PACKAGE Package, PINFCONTEXT ModelLine, PWSTR Section, DWORD SectionSize)
{
    WCHAR Install[MAX_INF_SECTION_NAME_LENGTH];

    if (!SetupGetStringFieldW(ModelLine, 1, Install, ARRAYSIZE(Install), NULL) || !Install[0])
        return FALSE;

    return SetupDiGetActualSectionToInstallW(Package->Inf, Install, Section, SectionSize, NULL, NULL);
}

typedef BOOL (*PDIFX_MODEL_CALLBACK)(PDIFX_PACKAGE Package, PINFCONTEXT ModelLine, PVOID Context);

static
BOOL
DifxForEachModelLine(PDIFX_PACKAGE Package, PDIFX_MODEL_CALLBACK Callback, PVOID Context)
{
    WCHAR Models[MAX_INF_SECTION_NAME_LENGTH];
    INFCONTEXT ManufacturerLine, ModelLine;
    BOOL Found;

    if (!SetupFindFirstLineW(Package->Inf, L"Manufacturer", NULL, &ManufacturerLine))
        return FALSE;

    do
    {
        if (!SetupDiGetActualModelsSectionW(&ManufacturerLine, NULL, Models, ARRAYSIZE(Models), NULL, NULL))
            continue;

        for (Found = SetupFindFirstLineW(Package->Inf, Models, NULL, &ModelLine);
             Found;
             Found = SetupFindNextLine(&ModelLine, &ModelLine))
        {
            if (!Callback(Package, &ModelLine, Context))
                return FALSE;
        }
    } while (SetupFindNextLine(&ManufacturerLine, &ManufacturerLine));

    return TRUE;
}

typedef struct _DIFX_ID_LIST
{
    PWSTR Ids;
    SIZE_T Used;
    BOOL Overflow;
} DIFX_ID_LIST, *PDIFX_ID_LIST;

static
BOOL
DifxIdListContains(PDIFX_ID_LIST List, PCWSTR Id)
{
    PCWSTR Current;

    for (Current = List->Ids; Current < List->Ids + List->Used; Current += wcslen(Current) + 1)
    {
        if (!_wcsicmp(Current, Id))
            return TRUE;
    }

    return FALSE;
}

static
BOOL
DifxCollectModelIds(PDIFX_PACKAGE Package, PINFCONTEXT ModelLine, PVOID Context)
{
    PDIFX_ID_LIST List = Context;
    WCHAR Id[MAX_DEVICE_ID_LEN];
    DWORD Field, Count, Length;

    UNREFERENCED_PARAMETER(Package);

    Count = SetupGetFieldCount(ModelLine);
    for (Field = 2; Field <= Count; Field++)
    {
        if (!SetupGetStringFieldW(ModelLine, Field, Id, ARRAYSIZE(Id), NULL) || !Id[0] || DifxIdListContains(List, Id))
            continue;

        Length = (DWORD)wcslen(Id) + 1;
        if (List->Used + Length + 1 > DIFX_ID_LIST_SIZE)
        {
            List->Overflow = TRUE;
            return FALSE;
        }

        CopyMemory(List->Ids + List->Used, Id, Length * sizeof(WCHAR));
        List->Used += Length;
        List->Ids[List->Used] = UNICODE_NULL;
    }

    return TRUE;
}

static
DWORD
DifxGetModelIds(PDIFX_PACKAGE Package, PDIFX_ID_LIST List)
{
    List->Ids = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, DIFX_ID_LIST_SIZE * sizeof(WCHAR));
    List->Used = 0;
    List->Overflow = FALSE;
    if (!List->Ids)
        return ERROR_OUTOFMEMORY;

    DifxForEachModelLine(Package, DifxCollectModelIds, List);
    if (List->Overflow)
        return ERROR_OUTOFMEMORY;
    if (!List->Used)
        return ERROR_NO_DEVICE_ID;

    return ERROR_SUCCESS;
}

static
VOID
DifxFreeIdList(PDIFX_ID_LIST List)
{
    HeapFree(GetProcessHeap(), 0, List->Ids);
    List->Ids = NULL;
}

typedef struct _DIFX_FILE_CHECK
{
    DWORD Error;
} DIFX_FILE_CHECK, *PDIFX_FILE_CHECK;

static
BOOL
DifxSourceFileExists(PDIFX_PACKAGE Package, PCWSTR SourceName)
{
    WCHAR SubDirectory[MAX_PATH], TagFile[MAX_PATH], Path[MAX_PATH];
    UINT SourceId;
    SIZE_T Length;

    SubDirectory[0] = UNICODE_NULL;
    if (!SetupGetSourceFileLocationW(Package->Inf, NULL, SourceName, &SourceId, SubDirectory, ARRAYSIZE(SubDirectory), NULL))
        return FALSE;

    if (SUCCEEDED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%ls\\%ls%ls%ls", Package->Directory, SubDirectory,
                                   SubDirectory[0] ? L"\\" : L"", SourceName)))
    {
        if (GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES)
            return TRUE;

        Length = wcslen(Path);
        Path[Length - 1] = L'_';
        if (GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES)
            return TRUE;
    }

    if (!SetupGetSourceInfoW(Package->Inf, SourceId, SRCINFO_TAGFILE, TagFile, ARRAYSIZE(TagFile), NULL) || !TagFile[0])
        return FALSE;

    Length = wcslen(TagFile);
    if (Length < 4 || _wcsicmp(TagFile + Length - 4, L".cab"))
        return FALSE;

    return SUCCEEDED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%ls\\%ls%ls%ls", Package->Directory, SubDirectory,
                                      SubDirectory[0] ? L"\\" : L"", TagFile)) &&
           GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

static
BOOL
DifxCheckCopyFileLine(PDIFX_PACKAGE Package, PINFCONTEXT FileLine)
{
    WCHAR Source[MAX_PATH];

    if ((!SetupGetStringFieldW(FileLine, 2, Source, ARRAYSIZE(Source), NULL) || !Source[0]) &&
        !SetupGetStringFieldW(FileLine, 1, Source, ARRAYSIZE(Source), NULL))
    {
        return TRUE;
    }

    return DifxSourceFileExists(Package, Source);
}

static
BOOL
DifxCheckCopyFilesValue(PDIFX_PACKAGE Package, PCWSTR Value)
{
    INFCONTEXT FileLine;
    BOOL Found;

    if (Value[0] == L'@')
        return DifxSourceFileExists(Package, Value + 1);

    for (Found = SetupFindFirstLineW(Package->Inf, Value, NULL, &FileLine);
         Found;
         Found = SetupFindNextLine(&FileLine, &FileLine))
    {
        if (!DifxCheckCopyFileLine(Package, &FileLine))
            return FALSE;
    }

    return TRUE;
}

static
BOOL
DifxCheckModelFiles(PDIFX_PACKAGE Package, PINFCONTEXT ModelLine, PVOID Context)
{
    PDIFX_FILE_CHECK Check = Context;
    WCHAR Section[MAX_INF_SECTION_NAME_LENGTH], Value[MAX_INF_SECTION_NAME_LENGTH];
    INFCONTEXT CopyLine;
    DWORD Field, Count;
    BOOL Found;

    if (!DifxGetInstallSection(Package, ModelLine, Section, ARRAYSIZE(Section)))
        return TRUE;

    for (Found = SetupFindFirstLineW(Package->Inf, Section, L"CopyFiles", &CopyLine);
         Found;
         Found = SetupFindNextMatchLineW(&CopyLine, L"CopyFiles", &CopyLine))
    {
        Count = SetupGetFieldCount(&CopyLine);
        for (Field = 1; Field <= Count; Field++)
        {
            if (!SetupGetStringFieldW(&CopyLine, Field, Value, ARRAYSIZE(Value), NULL) || !Value[0])
                continue;
            if (!DifxCheckCopyFilesValue(Package, Value))
            {
                Check->Error = ERROR_MISSING_FILE;
                return FALSE;
            }
        }
    }

    return TRUE;
}

static
DWORD
DifxCheckPackageFiles(PDIFX_PACKAGE Package)
{
    DIFX_FILE_CHECK Check = {ERROR_SUCCESS};

    DifxForEachModelLine(Package, DifxCheckModelFiles, &Check);
    return Check.Error;
}

static
DWORD
DifxGetDeviceIdRank(PCWSTR Ids, PCWSTR MatchingId, DWORD Base)
{
    PCWSTR Current;
    DWORD Index = 0;

    if (!Ids)
        return MAXDWORD;

    for (Current = Ids; *Current; Current += wcslen(Current) + 1, Index++)
    {
        if (!_wcsicmp(Current, MatchingId))
            return Base + Index * 0x100;
    }

    return MAXDWORD;
}

static
PWSTR
DifxGetDeviceIds(HDEVINFO DeviceSet, PSP_DEVINFO_DATA Device, DWORD Property)
{
    DWORD Size = 0;
    PWSTR Ids;

    SetupDiGetDeviceRegistryPropertyW(DeviceSet, Device, Property, NULL, NULL, 0, &Size);
    if (!Size)
        return NULL;

    Ids = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size + 2 * sizeof(WCHAR));
    if (Ids && !SetupDiGetDeviceRegistryPropertyW(DeviceSet, Device, Property, NULL, (PBYTE)Ids, Size, NULL))
    {
        HeapFree(GetProcessHeap(), 0, Ids);
        Ids = NULL;
    }

    return Ids;
}

static
BOOL
DifxDeviceMatchesPackage(PDIFX_ID_LIST PackageIds, PCWSTR DeviceIds)
{
    PCWSTR Current;

    if (!DeviceIds)
        return FALSE;

    for (Current = DeviceIds; *Current; Current += wcslen(Current) + 1)
    {
        if (DifxIdListContains(PackageIds, Current))
            return TRUE;
    }

    return FALSE;
}

static
BOOL
DifxIsPackageBetterForDevice(PDIFX_PACKAGE Package, HDEVINFO DeviceSet, PSP_DEVINFO_DATA Device,
                             PCWSTR HardwareIds, PCWSTR CompatibleIds)
{
    SP_DEVINSTALL_PARAMS_W InstallParams;
    SP_DRVINFO_DATA_W Driver;
    SP_DRVINSTALL_PARAMS DriverParams;
    WCHAR MatchingId[MAX_DEVICE_ID_LEN + 1], Version[64];
    FILETIME InstalledDate = {0, 0};
    ULARGE_INTEGER InstalledVersion;
    DWORD InstalledRank, Size, Type;
    DWORD Parts[4] = {0, 0, 0, 0};
    LONG Compare;
    HKEY Key;
    BOOL Better = TRUE;

    InstallParams.cbSize = sizeof(InstallParams);
    if (!SetupDiGetDeviceInstallParamsW(DeviceSet, Device, &InstallParams))
        return FALSE;
    InstallParams.Flags |= DI_ENUMSINGLEINF;
    StringCchCopyW(InstallParams.DriverPath, ARRAYSIZE(InstallParams.DriverPath), Package->InfPath);
    if (!SetupDiSetDeviceInstallParamsW(DeviceSet, Device, &InstallParams) ||
        !SetupDiBuildDriverInfoList(DeviceSet, Device, SPDIT_COMPATDRIVER) ||
        !SetupDiCallClassInstaller(DIF_SELECTBESTCOMPATDRV, DeviceSet, Device))
    {
        return FALSE;
    }

    Driver.cbSize = sizeof(Driver);
    DriverParams.cbSize = sizeof(DriverParams);
    if (!SetupDiGetSelectedDriverW(DeviceSet, Device, &Driver) ||
        !SetupDiGetDriverInstallParamsW(DeviceSet, Device, &Driver, &DriverParams))
    {
        return FALSE;
    }

    Key = SetupDiOpenDevRegKey(DeviceSet, Device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
    if (Key == INVALID_HANDLE_VALUE)
        return TRUE;

    Size = sizeof(MatchingId) - sizeof(WCHAR);
    if (RegQueryValueExW(Key, REGSTR_VAL_MATCHINGDEVID, NULL, &Type, (LPBYTE)MatchingId, &Size) != ERROR_SUCCESS ||
        Type != REG_SZ)
    {
        RegCloseKey(Key);
        return TRUE;
    }
    MatchingId[Size / sizeof(WCHAR)] = UNICODE_NULL;

    InstalledRank = DifxGetDeviceIdRank(HardwareIds, MatchingId, 0);
    if (InstalledRank == MAXDWORD)
        InstalledRank = DifxGetDeviceIdRank(CompatibleIds, MatchingId, 0x2000);

    Size = sizeof(InstalledDate);
    if (RegQueryValueExW(Key, L"DriverDateData", NULL, &Type, (LPBYTE)&InstalledDate, &Size) != ERROR_SUCCESS ||
        Type != REG_BINARY || Size != sizeof(InstalledDate))
    {
        InstalledDate.dwLowDateTime = InstalledDate.dwHighDateTime = 0;
    }

    Size = sizeof(Version) - sizeof(WCHAR);
    if (RegQueryValueExW(Key, L"DriverVersion", NULL, &Type, (LPBYTE)Version, &Size) == ERROR_SUCCESS && Type == REG_SZ)
    {
        Version[Size / sizeof(WCHAR)] = UNICODE_NULL;
        swscanf(Version, L"%lu.%lu.%lu.%lu", &Parts[0], &Parts[1], &Parts[2], &Parts[3]);
    }
    RegCloseKey(Key);

    InstalledVersion.HighPart = (Parts[0] << 16) | (Parts[1] & 0xffff);
    InstalledVersion.LowPart = (Parts[2] << 16) | (Parts[3] & 0xffff);

    if (DriverParams.Rank != InstalledRank)
        return DriverParams.Rank < InstalledRank;

    Compare = CompareFileTime(&Driver.DriverDate, &InstalledDate);
    if (Compare != 0)
        Better = Compare > 0;
    else
        Better = Driver.DriverVersion > InstalledVersion.QuadPart;

    return Better;
}

static
DWORD
DifxCheckDevicePresence(PDIFX_PACKAGE Package, PDIFX_ID_LIST PackageIds, BOOL Force)
{
    SP_DEVINFO_DATA Device;
    HDEVINFO DeviceSet;
    PWSTR HardwareIds, CompatibleIds;
    BOOL Matched = FALSE, Better = FALSE;
    DWORD Index;

    DeviceSet = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES);
    if (DeviceSet == INVALID_HANDLE_VALUE)
        return GetLastError();

    Device.cbSize = sizeof(Device);
    for (Index = 0; !Better && SetupDiEnumDeviceInfo(DeviceSet, Index, &Device); Index++)
    {
        HardwareIds = DifxGetDeviceIds(DeviceSet, &Device, SPDRP_HARDWAREID);
        CompatibleIds = DifxGetDeviceIds(DeviceSet, &Device, SPDRP_COMPATIBLEIDS);
        if (DifxDeviceMatchesPackage(PackageIds, HardwareIds) || DifxDeviceMatchesPackage(PackageIds, CompatibleIds))
        {
            Matched = TRUE;
            Better = Force || DifxIsPackageBetterForDevice(Package, DeviceSet, &Device, HardwareIds, CompatibleIds);
        }
        HeapFree(GetProcessHeap(), 0, HardwareIds);
        HeapFree(GetProcessHeap(), 0, CompatibleIds);
    }

    SetupDiDestroyDeviceInfoList(DeviceSet);

    if (!Matched)
        return ERROR_NO_SUCH_DEVINST;
    if (!Better)
        return ERROR_NO_MORE_ITEMS;
    return ERROR_SUCCESS;
}

static
DWORD
DifxValidatePackage(PDIFX_PACKAGE Package, DWORD UnsupportedTypeError)
{
    DWORD Error;

    if (!DifxIsFunctionDriverPackage(Package))
        return UnsupportedTypeError;

    Error = DifxVerifyPackageSignature(Package);
    if (Error != ERROR_SUCCESS)
        return Error;

    return DifxCheckPackageFiles(Package);
}

static
DWORD
DifxStagePackage(PDIFX_PACKAGE Package, BOOL Replace)
{
    DWORD Error;

    if (SetupCopyOEMInfW(Package->InfPath, NULL, SPOST_PATH, Replace ? 0 : SP_COPY_NOOVERWRITE, NULL, 0, NULL, NULL))
        return ERROR_SUCCESS;

    Error = GetLastError();
    return Error == ERROR_FILE_EXISTS ? ERROR_ALREADY_EXISTS : Error;
}

static
DWORD
DifxPreinstall(PCWSTR InfPath, DWORD Flags)
{
    DIFX_PACKAGE Package;
    DIFX_ID_LIST Ids;
    DWORD Error;

    Error = DifxCheckCaller();
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = DifxOpenPackage(InfPath, &Package);
    if (Error == ERROR_SUCCESS)
        Error = DifxValidatePackage(&Package, ERROR_INVALID_FUNCTION);

    if (Error == ERROR_SUCCESS && (Flags & DRIVER_PACKAGE_ONLY_IF_DEVICE_PRESENT))
    {
        Error = DifxGetModelIds(&Package, &Ids);
        if (Error == ERROR_SUCCESS)
            Error = DifxCheckDevicePresence(&Package, &Ids, !!(Flags & DRIVER_PACKAGE_FORCE));
        DifxFreeIdList(&Ids);
    }

    if (Error == ERROR_SUCCESS)
        Error = DifxStagePackage(&Package, !!(Flags & DRIVER_PACKAGE_REPAIR));

    DifxClosePackage(&Package);
    return Error;
}

static
BOOL
DifxIsInstallerInfoValid(PCINSTALLERINFO_W Info)
{
    if (!Info)
        return TRUE;

    return Info->pApplicationId && Info->pApplicationId[0] &&
           Info->pDisplayName && Info->pDisplayName[0] &&
           Info->pProductName && Info->pProductName[0] &&
           Info->pMfgName && Info->pMfgName[0];
}

static
DWORD
DifxInstallForDevices(PDIFX_PACKAGE Package, PDIFX_ID_LIST Ids, DWORD Flags, BOOL *NeedReboot)
{
    DWORD InstallFlags = INSTALLFLAG_NONINTERACTIVE, Error, Result = ERROR_NO_SUCH_DEVINST;
    PCWSTR Id;
    BOOL Reboot;

    if (Flags & (DRIVER_PACKAGE_FORCE | DRIVER_PACKAGE_REPAIR))
        InstallFlags |= INSTALLFLAG_FORCE;

    for (Id = Ids->Ids; *Id; Id += wcslen(Id) + 1)
    {
        Reboot = FALSE;
        if (UpdateDriverForPlugAndPlayDevicesW(NULL, Id, Package->InfPath, InstallFlags, &Reboot))
        {
            Result = ERROR_SUCCESS;
            if (Reboot)
                *NeedReboot = TRUE;
            continue;
        }

        Error = GetLastError();
        if (Error == ERROR_NO_SUCH_DEVINST)
            continue;
        if (Error == ERROR_NO_MORE_ITEMS)
        {
            if (Result == ERROR_NO_SUCH_DEVINST)
                Result = ERROR_NO_MORE_ITEMS;
            continue;
        }

        DifxLog(DIFXAPI_ERROR, Error, L"Installing '%ls' for hardware ID '%ls' failed with 0x%08lx", Package->InfPath, Id, Error);
        if (Result != ERROR_SUCCESS)
            Result = ERROR_INSTALL_FAILURE;
    }

    return Result;
}

static
DWORD
DifxInstall(PCWSTR InfPath, DWORD Flags, PCINSTALLERINFO_W Info, BOOL *NeedReboot)
{
    DIFX_PACKAGE Package;
    DIFX_ID_LIST Ids = {NULL, 0, FALSE};
    DWORD Error;

    if (NeedReboot)
        *NeedReboot = FALSE;

    Error = DifxCheckCaller();
    if (Error != ERROR_SUCCESS)
        return Error;
    if (!NeedReboot || !DifxIsInstallerInfoValid(Info))
        return ERROR_INVALID_PARAMETER;

    Error = DifxOpenPackage(InfPath, &Package);
    if (Error == ERROR_SUCCESS)
        Error = DifxValidatePackage(&Package, ERROR_UNSUPPORTED_TYPE);
    if (Error == ERROR_SUCCESS)
        Error = DifxGetModelIds(&Package, &Ids);
    if (Error == ERROR_SUCCESS && (Flags & DRIVER_PACKAGE_ONLY_IF_DEVICE_PRESENT))
        Error = DifxCheckDevicePresence(&Package, &Ids, !!(Flags & DRIVER_PACKAGE_FORCE));
    if (Error == ERROR_SUCCESS)
    {
        Error = DifxStagePackage(&Package, TRUE);
        if (Error == ERROR_SUCCESS)
            Error = DifxInstallForDevices(&Package, &Ids, Flags, NeedReboot);
    }

    /* TODO: INFO_NEEDED record the INSTALLERINFO application association and its Programs and Features entry */

    DifxFreeIdList(&Ids);
    DifxClosePackage(&Package);
    return Error;
}

static
BOOL
DifxFilesAreIdentical(PCWSTR First, PCWSTR Second)
{
    BYTE BufferFirst[4096], BufferSecond[4096];
    HANDLE FileFirst, FileSecond;
    DWORD ReadFirst, ReadSecond;
    BOOL Identical = FALSE;

    FileFirst = CreateFileW(First, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (FileFirst == INVALID_HANDLE_VALUE)
        return FALSE;

    FileSecond = CreateFileW(Second, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (FileSecond == INVALID_HANDLE_VALUE)
    {
        CloseHandle(FileFirst);
        return FALSE;
    }

    if (GetFileSize(FileFirst, NULL) == GetFileSize(FileSecond, NULL))
    {
        for (;;)
        {
            if (!ReadFile(FileFirst, BufferFirst, sizeof(BufferFirst), &ReadFirst, NULL) ||
                !ReadFile(FileSecond, BufferSecond, sizeof(BufferSecond), &ReadSecond, NULL) ||
                ReadFirst != ReadSecond || memcmp(BufferFirst, BufferSecond, ReadFirst))
            {
                break;
            }
            if (!ReadFirst)
            {
                Identical = TRUE;
                break;
            }
        }
    }

    CloseHandle(FileSecond);
    CloseHandle(FileFirst);
    return Identical;
}

static
DWORD
DifxFindPublishedInf(PCWSTR InfPath, PWSTR Published, DWORD PublishedSize)
{
    WCHAR StoreInf[MAX_PATH], InfDirectory[MAX_PATH], Pattern[MAX_PATH];
    WIN32_FIND_DATAW FindData;
    SYSTEM_INFO SystemInfo;
    DWORD Length = ARRAYSIZE(StoreInf);
    HANDLE Find;
    HRESULT hr;
    UINT DirectoryLength;

    GetNativeSystemInfo(&SystemInfo);
    hr = DriverStoreFindDriverPackageW(InfPath, NULL, NULL, SystemInfo.wProcessorArchitecture, NULL, StoreInf, &Length);
    if (FAILED(hr))
        return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? ERROR_FILE_NOT_FOUND : ERROR_DRIVER_PACKAGE_NOT_IN_STORE;

    DirectoryLength = GetSystemWindowsDirectoryW(InfDirectory, ARRAYSIZE(InfDirectory));
    if (!DirectoryLength || DirectoryLength >= ARRAYSIZE(InfDirectory) ||
        FAILED(StringCchCatW(InfDirectory, ARRAYSIZE(InfDirectory), L"\\INF")) ||
        FAILED(StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%ls\\oem*.inf", InfDirectory)))
    {
        return ERROR_DRIVER_PACKAGE_NOT_IN_STORE;
    }

    Find = FindFirstFileW(Pattern, &FindData);
    if (Find == INVALID_HANDLE_VALUE)
        return ERROR_DRIVER_PACKAGE_NOT_IN_STORE;

    do
    {
        if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            FAILED(StringCchPrintfW(Published, PublishedSize, L"%ls\\%ls", InfDirectory, FindData.cFileName)))
        {
            continue;
        }
        if (DifxFilesAreIdentical(Published, StoreInf))
        {
            FindClose(Find);
            return ERROR_SUCCESS;
        }
    } while (FindNextFileW(Find, &FindData));

    FindClose(Find);
    return ERROR_DRIVER_PACKAGE_NOT_IN_STORE;
}

static
DWORD
DifxLocatePackage(PCWSTR InfPath, PDIFX_PACKAGE Package, PWSTR Published, DWORD PublishedSize)
{
    DWORD Error;

    Error = DifxResolvePackagePath(InfPath, Package);
    if (Error != ERROR_SUCCESS)
        return Error;

    if (!DifxIsAdministrator())
        return ERROR_ACCESS_DENIED;

    return DifxFindPublishedInf(Package->InfPath, Published, PublishedSize);
}

static
DWORD
DifxUninstall(PCWSTR InfPath, DWORD Flags, PCINSTALLERINFO_W Info, BOOL *NeedReboot)
{
    WCHAR Published[MAX_PATH];
    DIFX_PACKAGE Package;
    PCWSTR PublishedName;
    DWORD Error;

    if (NeedReboot)
        *NeedReboot = FALSE;

    Error = DifxCheckCaller();
    if (Error != ERROR_SUCCESS)
        return Error;
    if (!NeedReboot || !DifxIsInstallerInfoValid(Info))
        return ERROR_INVALID_PARAMETER;

    Error = DifxLocatePackage(InfPath, &Package, Published, ARRAYSIZE(Published));
    if (Error != ERROR_SUCCESS)
        return Error;

    /* TODO: INFO_NEEDED remove the INSTALLERINFO association and report ERROR_DEPENDENT_APPLICATIONS_EXIST while others remain */

    PublishedName = wcsrchr(Published, L'\\') + 1;
    if (!SetupUninstallOEMInfW(PublishedName, (Flags & DRIVER_PACKAGE_FORCE) ? SUOI_FORCEDELETE : 0, NULL))
        return GetLastError();

    return ERROR_SUCCESS;
}

static
DWORD
DifxGetPath(PCWSTR InfPath, PWSTR DestInfPath, DWORD *NumOfChars)
{
    WCHAR Published[MAX_PATH];
    DIFX_PACKAGE Package;
    DWORD Error, Length;

    Error = DifxCheckCaller();
    if (Error != ERROR_SUCCESS)
        return Error;
    if (!NumOfChars)
        return ERROR_INVALID_PARAMETER;

    Error = DifxLocatePackage(InfPath, &Package, Published, ARRAYSIZE(Published));
    if (Error != ERROR_SUCCESS)
        return Error;

    /* TODO: INFO_NEEDED whether Windows 11 returns the published INF or the driver store copy */
    Length = (DWORD)wcslen(Published) + 1;
    if (!DestInfPath || *NumOfChars < Length)
    {
        *NumOfChars = Length;
        return ERROR_INSUFFICIENT_BUFFER;
    }

    StringCchCopyW(DestInfPath, *NumOfChars, Published);
    *NumOfChars = Length;
    return ERROR_SUCCESS;
}

static
PWSTR
DifxAnsiToUnicode(PCSTR String)
{
    PWSTR Result;
    INT Length;

    if (!String)
        return NULL;

    Length = MultiByteToWideChar(CP_ACP, 0, String, -1, NULL, 0);
    Result = HeapAlloc(GetProcessHeap(), 0, Length * sizeof(WCHAR));
    if (Result)
        MultiByteToWideChar(CP_ACP, 0, String, -1, Result, Length);
    return Result;
}

static
BOOL
DifxInstallerInfoToUnicode(PCINSTALLERINFO_A InfoA, PINSTALLERINFO_W InfoW)
{
    ZeroMemory(InfoW, sizeof(*InfoW));
    if (!InfoA)
        return TRUE;

    InfoW->pApplicationId = DifxAnsiToUnicode(InfoA->pApplicationId);
    InfoW->pDisplayName = DifxAnsiToUnicode(InfoA->pDisplayName);
    InfoW->pProductName = DifxAnsiToUnicode(InfoA->pProductName);
    InfoW->pMfgName = DifxAnsiToUnicode(InfoA->pMfgName);
    return (!InfoA->pApplicationId || InfoW->pApplicationId) && (!InfoA->pDisplayName || InfoW->pDisplayName) &&
           (!InfoA->pProductName || InfoW->pProductName) && (!InfoA->pMfgName || InfoW->pMfgName);
}

static
VOID
DifxFreeInstallerInfo(PINSTALLERINFO_W InfoW)
{
    HeapFree(GetProcessHeap(), 0, InfoW->pApplicationId);
    HeapFree(GetProcessHeap(), 0, InfoW->pDisplayName);
    HeapFree(GetProcessHeap(), 0, InfoW->pProductName);
    HeapFree(GetProcessHeap(), 0, InfoW->pMfgName);
}

typedef DWORD (WINAPI *PDIFX_PACKAGE_OPERATION)(PCWSTR, DWORD, PCINSTALLERINFO_W, BOOL *);

static
DWORD
DifxCallWithAnsi(PDIFX_PACKAGE_OPERATION Operation, PCSTR InfPath, DWORD Flags, PCINSTALLERINFO_A InfoA, BOOL *NeedReboot)
{
    INSTALLERINFO_W InfoW;
    PWSTR InfPathW = NULL;
    DWORD Error;

    if (InfPath && !(InfPathW = DifxAnsiToUnicode(InfPath)))
        return ERROR_OUTOFMEMORY;

    if (!DifxInstallerInfoToUnicode(InfoA, &InfoW))
        Error = ERROR_OUTOFMEMORY;
    else
        Error = Operation(InfPathW, Flags, InfoA ? &InfoW : NULL, NeedReboot);

    DifxFreeInstallerInfo(&InfoW);
    HeapFree(GetProcessHeap(), 0, InfPathW);
    return Error;
}

DWORD
WINAPI
DriverPackagePreinstallW(PCWSTR DriverPackageInfPath, DWORD Flags)
{
    TRACE("(%s, 0x%lx)\n", debugstr_w(DriverPackageInfPath), Flags);
    return DifxResult(L"DriverPackagePreinstall", DriverPackageInfPath, DifxPreinstall(DriverPackageInfPath, Flags));
}

DWORD
WINAPI
DriverPackagePreinstallA(PCSTR DriverPackageInfPath, DWORD Flags)
{
    PWSTR InfPathW = NULL;
    DWORD Error;

    TRACE("(%s, 0x%lx)\n", debugstr_a(DriverPackageInfPath), Flags);
    if (DriverPackageInfPath && !(InfPathW = DifxAnsiToUnicode(DriverPackageInfPath)))
        return ERROR_OUTOFMEMORY;

    Error = DriverPackagePreinstallW(InfPathW, Flags);
    HeapFree(GetProcessHeap(), 0, InfPathW);
    return Error;
}

DWORD
WINAPI
DriverPackageInstallW(PCWSTR DriverPackageInfPath, DWORD Flags, PCINSTALLERINFO_W pInstallerInfo, BOOL *pNeedReboot)
{
    TRACE("(%s, 0x%lx, %p, %p)\n", debugstr_w(DriverPackageInfPath), Flags, pInstallerInfo, pNeedReboot);
    return DifxResult(L"DriverPackageInstall", DriverPackageInfPath,
                      DifxInstall(DriverPackageInfPath, Flags, pInstallerInfo, pNeedReboot));
}

DWORD
WINAPI
DriverPackageInstallA(PCSTR DriverPackageInfPath, DWORD Flags, PCINSTALLERINFO_A pInstallerInfo, BOOL *pNeedReboot)
{
    TRACE("(%s, 0x%lx, %p, %p)\n", debugstr_a(DriverPackageInfPath), Flags, pInstallerInfo, pNeedReboot);
    return DifxCallWithAnsi(DriverPackageInstallW, DriverPackageInfPath, Flags, pInstallerInfo, pNeedReboot);
}

DWORD
WINAPI
DriverPackageUninstallW(PCWSTR DriverPackageInfPath, DWORD Flags, PCINSTALLERINFO_W pInstallerInfo, BOOL *pNeedReboot)
{
    TRACE("(%s, 0x%lx, %p, %p)\n", debugstr_w(DriverPackageInfPath), Flags, pInstallerInfo, pNeedReboot);
    return DifxResult(L"DriverPackageUninstall", DriverPackageInfPath,
                      DifxUninstall(DriverPackageInfPath, Flags, pInstallerInfo, pNeedReboot));
}

DWORD
WINAPI
DriverPackageUninstallA(PCSTR DriverPackageInfPath, DWORD Flags, PCINSTALLERINFO_A pInstallerInfo, BOOL *pNeedReboot)
{
    TRACE("(%s, 0x%lx, %p, %p)\n", debugstr_a(DriverPackageInfPath), Flags, pInstallerInfo, pNeedReboot);
    return DifxCallWithAnsi(DriverPackageUninstallW, DriverPackageInfPath, Flags, pInstallerInfo, pNeedReboot);
}

DWORD
WINAPI
DriverPackageGetPathW(PCWSTR DriverPackageInfPath, PWSTR pDestInfPath, DWORD *pNumOfChars)
{
    TRACE("(%s, %p, %p)\n", debugstr_w(DriverPackageInfPath), pDestInfPath, pNumOfChars);
    return DifxResult(L"DriverPackageGetPath", DriverPackageInfPath,
                      DifxGetPath(DriverPackageInfPath, pDestInfPath, pNumOfChars));
}

DWORD
WINAPI
DriverPackageGetPathA(PCSTR DriverPackageInfPath, PSTR pDestInfPath, DWORD *pNumOfChars)
{
    WCHAR DestW[MAX_PATH];
    PWSTR InfPathW = NULL;
    DWORD Error, CharsW = ARRAYSIZE(DestW);
    INT Length;

    TRACE("(%s, %p, %p)\n", debugstr_a(DriverPackageInfPath), pDestInfPath, pNumOfChars);
    if (!pNumOfChars)
        return DriverPackageGetPathW(NULL, NULL, NULL);
    if (DriverPackageInfPath && !(InfPathW = DifxAnsiToUnicode(DriverPackageInfPath)))
        return ERROR_OUTOFMEMORY;

    Error = DriverPackageGetPathW(InfPathW, DestW, &CharsW);
    HeapFree(GetProcessHeap(), 0, InfPathW);
    if (Error != ERROR_SUCCESS)
        return Error;

    Length = WideCharToMultiByte(CP_ACP, 0, DestW, -1, NULL, 0, NULL, NULL);
    if (!pDestInfPath || *pNumOfChars < (DWORD)Length)
    {
        *pNumOfChars = Length;
        return ERROR_INSUFFICIENT_BUFFER;
    }

    WideCharToMultiByte(CP_ACP, 0, DestW, -1, pDestInfPath, *pNumOfChars, NULL, NULL);
    *pNumOfChars = Length;
    return ERROR_SUCCESS;
}

VOID
WINAPI
SetDifxLogCallbackW(DIFXLOGCALLBACK_W LogCallback, PVOID CallbackContext)
{
    TRACE("(%p, %p)\n", LogCallback, CallbackContext);
    EnterCriticalSection(&DifxLogLock);
    DifxLogger.WinApiW = LogCallback;
    DifxLogger.WinApiA = NULL;
    DifxLogger.WinApiContext = CallbackContext;
    LeaveCriticalSection(&DifxLogLock);
}

VOID
WINAPI
SetDifxLogCallbackA(DIFXLOGCALLBACK_A LogCallback, PVOID CallbackContext)
{
    TRACE("(%p, %p)\n", LogCallback, CallbackContext);
    EnterCriticalSection(&DifxLogLock);
    DifxLogger.WinApiA = LogCallback;
    DifxLogger.WinApiW = NULL;
    DifxLogger.WinApiContext = CallbackContext;
    LeaveCriticalSection(&DifxLogLock);
}

VOID
WINAPI
DIFXAPISetLogCallbackW(DIFXAPILOGCALLBACK_W LogCallback, PVOID CallbackContext)
{
    TRACE("(%p, %p)\n", LogCallback, CallbackContext);
    EnterCriticalSection(&DifxLogLock);
    DifxLogger.CdeclW = LogCallback;
    DifxLogger.CdeclA = NULL;
    DifxLogger.CdeclContext = CallbackContext;
    LeaveCriticalSection(&DifxLogLock);
}

VOID
WINAPI
DIFXAPISetLogCallbackA(DIFXAPILOGCALLBACK_A LogCallback, PVOID CallbackContext)
{
    TRACE("(%p, %p)\n", LogCallback, CallbackContext);
    EnterCriticalSection(&DifxLogLock);
    DifxLogger.CdeclA = LogCallback;
    DifxLogger.CdeclW = NULL;
    DifxLogger.CdeclContext = CallbackContext;
    LeaveCriticalSection(&DifxLogLock);
}

BOOL
WINAPI
DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);

    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        InitializeCriticalSection(&DifxLogLock);
    }
    else if (Reason == DLL_PROCESS_DETACH)
    {
        DeleteCriticalSection(&DifxLogLock);
    }

    return TRUE;
}
