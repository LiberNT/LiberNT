/*
 * PROJECT:     LiberNT DISM
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Deployment Image Servicing and Management imaging commands
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winerror.h>
#include <ntverp.h>
#include <strsafe.h>
#include <conutils.h>
#include <wimlib.h>
#include "resource.h"

#define DISM_E_IMAGE_NOT_FOUND  0xC1560111UL
#define DISM_BAR_WIDTH          58
#define DISM_MAX_OPTIONS        64
#define DISM_SOLID_WIM_VERSION  0xE00

typedef struct _DISM_OPTION
{
    WCHAR Name[64];
    PCWSTR Value;
} DISM_OPTION, *PDISM_OPTION;

typedef DWORD (*PDISM_HANDLER)(VOID);

typedef struct _DISM_COMMAND
{
    PCWSTR Name;
    UINT HelpId;
    PDISM_HANDLER Handler;
    PCWSTR const *Options;
} DISM_COMMAND, *PDISM_COMMAND;

static DISM_OPTION DismOptions[DISM_MAX_OPTIONS];
static ULONG DismOptionCount;
static const DISM_COMMAND *DismCommand;
static BOOL DismQuiet;
static LCID DismLcid = LOCALE_USER_DEFAULT;
static LANGID DismLangId = LANG_USER_DEFAULT;
static UINT DismErrorTextId;
static WCHAR DismErrorArg1[64];
static WCHAR DismErrorArg2[64];
static WCHAR DismLogPath[MAX_PATH];
static BOOL DismWimlibReady;
static BOOL DismProgressConsole;
static BOOL DismProgressShown;
static LONG DismProgressLast;

static const PCWSTR DismGlobalOptions[] =
{
    L"English", L"Quiet", L"LogPath", L"LogLevel", L"ScratchDir", L"NoRestart",
    L"Format", L"WinDir", L"SysDriveDir", NULL
};

static const PCWSTR DismGetImageInfoOptions[] = { L"ImageFile", L"Index", L"Name", NULL };
static const PCWSTR DismGetWimInfoOptions[] = { L"WimFile", L"Index", L"Name", NULL };
static const PCWSTR DismListImageOptions[] = { L"ImageFile", L"Index", L"Name", NULL };
static const PCWSTR DismApplyImageOptions[] =
{
    L"ImageFile", L"ApplyDir", L"Index", L"Name", L"CheckIntegrity", L"Verify", L"NoRpFix",
    L"SWMFile", L"ConfirmTrustedFile", L"WIMBoot", L"Compact", L"EA", NULL
};
static const PCWSTR DismCaptureImageOptions[] =
{
    L"ImageFile", L"CaptureDir", L"Name", L"Description", L"ConfigFile", L"Compress",
    L"Bootable", L"WIMBoot", L"CheckIntegrity", L"Verify", L"NoRpFix", L"EA", NULL
};
static const PCWSTR DismAppendImageOptions[] =
{
    L"ImageFile", L"CaptureDir", L"Name", L"Description", L"ConfigFile", L"Bootable",
    L"WIMBoot", L"CheckIntegrity", L"Verify", L"NoRpFix", L"EA", NULL
};
static const PCWSTR DismExportImageOptions[] =
{
    L"SourceImageFile", L"SWMFile", L"SourceIndex", L"SourceName", L"DestinationImageFile",
    L"DestinationName", L"Compress", L"Bootable", L"WIMBoot", L"CheckIntegrity", NULL
};
static const PCWSTR DismDeleteImageOptions[] = { L"ImageFile", L"Index", L"Name", L"CheckIntegrity", NULL };
static const PCWSTR DismSplitImageOptions[] = { L"ImageFile", L"SWMFile", L"FileSize", L"CheckIntegrity", NULL };
static const PCWSTR DismNoOptions[] = { NULL };

static DWORD DismGetImageInfo(VOID);
static DWORD DismGetWimInfo(VOID);
static DWORD DismListImage(VOID);
static DWORD DismApplyImage(VOID);
static DWORD DismCaptureImage(VOID);
static DWORD DismAppendImage(VOID);
static DWORD DismExportImage(VOID);
static DWORD DismDeleteImage(VOID);
static DWORD DismSplitImage(VOID);
static DWORD DismGetMountedImageInfo(VOID);
static DWORD DismCleanupMountpoints(VOID);
static DWORD DismNotSupported(VOID);

static const DISM_COMMAND DismCommands[] =
{
    { L"Get-ImageInfo", IDS_HELP_GET_IMAGEINFO, DismGetImageInfo, DismGetImageInfoOptions },
    { L"Get-WimInfo", IDS_HELP_GET_WIMINFO, DismGetWimInfo, DismGetWimInfoOptions },
    { L"List-Image", IDS_HELP_LIST_IMAGE, DismListImage, DismListImageOptions },
    { L"Apply-Image", IDS_HELP_APPLY_IMAGE, DismApplyImage, DismApplyImageOptions },
    { L"Capture-Image", IDS_HELP_CAPTURE_IMAGE, DismCaptureImage, DismCaptureImageOptions },
    { L"Append-Image", IDS_HELP_APPEND_IMAGE, DismAppendImage, DismAppendImageOptions },
    { L"Export-Image", IDS_HELP_EXPORT_IMAGE, DismExportImage, DismExportImageOptions },
    { L"Delete-Image", IDS_HELP_DELETE_IMAGE, DismDeleteImage, DismDeleteImageOptions },
    { L"Split-Image", IDS_HELP_SPLIT_IMAGE, DismSplitImage, DismSplitImageOptions },
    { L"Get-MountedImageInfo", IDS_HELP_GET_MOUNTED, DismGetMountedImageInfo, DismNoOptions },
    { L"Get-MountedWimInfo", IDS_HELP_GET_MOUNTED, DismGetMountedImageInfo, DismNoOptions },
    { L"Cleanup-Mountpoints", IDS_HELP_CLEANUP, DismCleanupMountpoints, DismNoOptions },
    { L"Cleanup-Wim", IDS_HELP_CLEANUP, DismCleanupMountpoints, DismNoOptions },
    { L"Mount-Image", 0, DismNotSupported, NULL },
    { L"Mount-Wim", 0, DismNotSupported, NULL },
    { L"Unmount-Image", 0, DismNotSupported, NULL },
    { L"Unmount-Wim", 0, DismNotSupported, NULL },
    { L"Commit-Image", 0, DismNotSupported, NULL },
    { L"Commit-Wim", 0, DismNotSupported, NULL },
    { L"Remount-Image", 0, DismNotSupported, NULL },
    { L"Remount-Wim", 0, DismNotSupported, NULL },
    { L"Online", 0, DismNotSupported, NULL },
    { L"Image", 0, DismNotSupported, NULL },
    { L"Capture-Ffu", 0, DismNotSupported, NULL },
    { L"Apply-Ffu", 0, DismNotSupported, NULL },
    { L"Split-Ffu", 0, DismNotSupported, NULL },
    { L"Optimize-Ffu", 0, DismNotSupported, NULL },
    { L"Capture-CustomImage", 0, DismNotSupported, NULL },
    { L"Apply-CustomDataImage", 0, DismNotSupported, NULL },
    { L"Get-WIMBootEntry", 0, DismNotSupported, NULL },
    { L"Update-WIMBootEntry", 0, DismNotSupported, NULL },
};

static VOID
DismLowerName(PWSTR Buffer, SIZE_T Count, PCWSTR Prefix, PCWSTR Name)
{
    PWSTR p;

    StringCchPrintfW(Buffer, Count, L"%s%s", Prefix, Name);
    for (p = Buffer; *p; p++)
        *p = towlower(*p);
}

static PDISM_OPTION
DismFindOption(PCWSTR Name)
{
    ULONG i;

    for (i = 0; i < DismOptionCount; i++)
    {
        if (!_wcsicmp(DismOptions[i].Name, Name))
            return &DismOptions[i];
    }
    return NULL;
}

static BOOL
DismHasOption(PCWSTR Name)
{
    return DismFindOption(Name) != NULL;
}

static PCWSTR
DismGetValue(PCWSTR Name)
{
    PDISM_OPTION Option = DismFindOption(Name);

    if (!Option || !Option->Value || !*Option->Value)
        return NULL;
    return Option->Value;
}

static BOOL
DismIsListed(PCWSTR const *List, PCWSTR Name)
{
    for (; List && *List; List++)
    {
        if (!_wcsicmp(*List, Name))
            return TRUE;
    }
    return FALSE;
}

static DWORD
DismFail(DWORD Error, UINT TextId)
{
    DismErrorTextId = TextId;
    return Error;
}

static DWORD
DismMissingOption(PCWSTR Name)
{
    DismLowerName(DismErrorArg1, ARRAYSIZE(DismErrorArg1), L"/", Name);
    return DismFail(ERROR_INVALID_PARAMETER, IDS_MISSING_OPTION);
}

static DWORD
DismOptionPair(UINT TextId, PCWSTR First, PCWSTR Second)
{
    DismLowerName(DismErrorArg1, ARRAYSIZE(DismErrorArg1), L"/", First);
    DismLowerName(DismErrorArg2, ARRAYSIZE(DismErrorArg2), L"/", Second);
    return DismFail(ERROR_INVALID_PARAMETER, TextId);
}

static VOID
DismLog(PCWSTR Format, ...)
{
    WCHAR Line[1024];
    CHAR Utf8[3 * ARRAYSIZE(Line)];
    SYSTEMTIME Time;
    HANDLE File;
    PWSTR End;
    size_t Remaining;
    va_list Args;
    int Length;
    DWORD Written;

    if (!DismLogPath[0])
        return;

    GetLocalTime(&Time);
    StringCchPrintfExW(Line, ARRAYSIZE(Line), &End, &Remaining, 0,
                       L"%04u-%02u-%02u %02u:%02u:%02u, Info                  DISM   ",
                       Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond);
    va_start(Args, Format);
    StringCchVPrintfExW(End, Remaining, &End, &Remaining, 0, Format, Args);
    va_end(Args);
    StringCchCatW(Line, ARRAYSIZE(Line), L"\r\n");

    Length = WideCharToMultiByte(CP_UTF8, 0, Line, -1, Utf8, sizeof(Utf8), NULL, NULL);
    if (Length <= 1)
        return;

    File = CreateFileW(DismLogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return;
    WriteFile(File, Utf8, Length - 1, &Written, NULL);
    CloseHandle(File);
}

static VOID
DismInitLog(VOID)
{
    PCWSTR Custom = DismGetValue(L"LogPath");
    WCHAR Directory[MAX_PATH];
    UINT Length;

    if (Custom)
    {
        GetFullPathNameW(Custom, ARRAYSIZE(DismLogPath), DismLogPath, NULL);
        return;
    }

    Length = GetWindowsDirectoryW(Directory, ARRAYSIZE(Directory));
    if (!Length || Length >= ARRAYSIZE(Directory))
        return;
    StringCchCatW(Directory, ARRAYSIZE(Directory), L"\\Logs");
    CreateDirectoryW(Directory, NULL);
    StringCchCatW(Directory, ARRAYSIZE(Directory), L"\\DISM");
    CreateDirectoryW(Directory, NULL);
    StringCchPrintfW(DismLogPath, ARRAYSIZE(DismLogPath), L"%s\\dism.log", Directory);
}

static VOID
DismPrintHeader(VOID)
{
    WCHAR Version[64];

    if (DismQuiet)
        return;
    StringCchPrintfW(Version, ARRAYSIZE(Version), L"%u.%u.%u.%u",
                     VER_PRODUCTMAJORVERSION, VER_PRODUCTMINORVERSION,
                     VER_PRODUCTBUILD, VER_PRODUCTBUILD_QFE);
    ConResPrintf(StdOut, IDS_HEADER, Version);
}

static VOID
DismPrintErrorText(DWORD Error)
{
    switch (DismErrorTextId)
    {
        case 0:
            ConMsgPuts(StdOut, FORMAT_MESSAGE_FROM_SYSTEM, NULL, Error, DismLangId);
            break;

        case IDS_MISSING_OPTION:
        case IDS_UNKNOWN_OPTION:
        case IDS_UNRECOGNIZED_TOKEN:
            ConResPrintf(StdOut, DismErrorTextId, DismErrorArg1);
            break;

        case IDS_OPTIONS_CONFLICT:
        case IDS_OPTION_REQUIRED:
            ConResPrintf(StdOut, DismErrorTextId, DismErrorArg1, DismErrorArg2);
            break;

        default:
            ConResPuts(StdOut, DismErrorTextId);
            break;
    }
}

static VOID
DismPrintError(DWORD Error)
{
    if (Error < 0x10000)
        ConResPrintf(StdOut, IDS_ERROR_DECIMAL, Error);
    else
        ConResPrintf(StdOut, IDS_ERROR_HEX, Error);
    DismPrintErrorText(Error);
    ConResPrintf(StdOut, IDS_LOG_LOCATION, DismLogPath);
}

static VOID
DismLoadString(UINT Id, PWSTR Buffer, INT Count)
{
    if (K32LoadStringExW(NULL, Id, DismLangId, Buffer, Count) <= 0)
        Buffer[0] = UNICODE_NULL;
}

static VOID
DismPrintBar(LONG Percent)
{
    WCHAR Bar[DISM_BAR_WIDTH + 1];
    WCHAR Text[16];
    LONG Filled = DISM_BAR_WIDTH * Percent / 100;
    size_t Length;
    LONG i, Start;

    StringCchPrintfW(Text, ARRAYSIZE(Text), L"%ld.0%%", Percent);
    Length = wcslen(Text);
    Start = (LONG)(DISM_BAR_WIDTH - Length + 1) / 2;
    for (i = 0; i < DISM_BAR_WIDTH; i++)
        Bar[i] = (i < Filled) ? L'=' : L' ';
    RtlCopyMemory(&Bar[Start], Text, Length * sizeof(WCHAR));
    Bar[DISM_BAR_WIDTH] = UNICODE_NULL;
    ConPrintf(StdOut, DismProgressConsole ? L"\r[%s] " : L"\r[%s] \n", Bar);
}

static VOID
DismBeginProgress(UINT TitleId)
{
    DismProgressShown = FALSE;
    DismProgressLast = 0;
    if (DismQuiet)
        return;
    DismProgressConsole = IsConsoleHandle(ConStreamGetOSHandle(StdOut));
    ConResPuts(StdOut, TitleId);
}

static VOID
DismUpdateProgress(ULONGLONG Done, ULONGLONG Total)
{
    LONG Percent;

    if (DismQuiet)
        return;
    Percent = Total ? (LONG)(min(Done, Total) * 100 / Total) : 100;
    if (Percent == 0 || Percent == DismProgressLast)
        return;
    DismProgressLast = Percent;
    DismProgressShown = TRUE;
    DismPrintBar(Percent);
}

static VOID
DismEndProgress(BOOL Success)
{
    if (DismQuiet)
        return;
    if (Success && DismProgressLast != 100)
        DismUpdateProgress(1, 1);
    if (DismProgressShown && DismProgressConsole)
        ConPuts(StdOut, L"\n");
}

static enum wimlib_progress_status
DismProgressCallback(enum wimlib_progress_msg Message,
                     union wimlib_progress_info *Info,
                     void *Context)
{
    UNREFERENCED_PARAMETER(Context);

    switch (Message)
    {
        case WIMLIB_PROGRESS_MSG_EXTRACT_STREAMS:
            DismUpdateProgress(Info->extract.completed_bytes, Info->extract.total_bytes);
            break;

        case WIMLIB_PROGRESS_MSG_WRITE_STREAMS:
            DismUpdateProgress(Info->write_streams.completed_bytes, Info->write_streams.total_bytes);
            break;

        default:
            break;
    }
    return WIMLIB_PROGRESS_STATUS_CONTINUE;
}

static DWORD
DismWimError(int Result)
{
    DWORD LastError = GetLastError();

    switch (Result)
    {
        case WIMLIB_ERR_SUCCESS:
            return ERROR_SUCCESS;

        case WIMLIB_ERR_NOMEM:
            return ERROR_NOT_ENOUGH_MEMORY;

        case WIMLIB_ERR_RESOURCE_NOT_FOUND:
            return ERROR_RESOURCE_DATA_NOT_FOUND;

        case WIMLIB_ERR_IS_SPLIT_WIM:
        case WIMLIB_ERR_WIM_IS_READONLY:
        case WIMLIB_ERR_ALREADY_LOCKED:
            return ERROR_ACCESS_DENIED;

        case WIMLIB_ERR_INSUFFICIENT_PRIVILEGES:
            return ERROR_PRIVILEGE_NOT_HELD;

        case WIMLIB_ERR_PATH_DOES_NOT_EXIST:
            return ERROR_PATH_NOT_FOUND;

        case WIMLIB_ERR_NOTDIR:
            return ERROR_DIRECTORY;

        case WIMLIB_ERR_UNSUPPORTED:
        case WIMLIB_ERR_WIMBOOT:
        case WIMLIB_ERR_WIM_IS_ENCRYPTED:
            return ERROR_NOT_SUPPORTED;

        case WIMLIB_ERR_DECOMPRESSION:
        case WIMLIB_ERR_INTEGRITY:
        case WIMLIB_ERR_INVALID_RESOURCE_HASH:
        case WIMLIB_ERR_INVALID_METADATA_RESOURCE:
        case WIMLIB_ERR_INVALID_LOOKUP_TABLE_ENTRY:
        case WIMLIB_ERR_INVALID_INTEGRITY_TABLE:
        case WIMLIB_ERR_UNEXPECTED_END_OF_FILE:
            return ERROR_FILE_CORRUPT;

        case WIMLIB_ERR_OPEN:
        case WIMLIB_ERR_OPENDIR:
        case WIMLIB_ERR_READ:
        case WIMLIB_ERR_WRITE:
        case WIMLIB_ERR_STAT:
        case WIMLIB_ERR_MKDIR:
        case WIMLIB_ERR_RENAME:
        case WIMLIB_ERR_LINK:
        case WIMLIB_ERR_READLINK:
        case WIMLIB_ERR_SET_ATTRIBUTES:
        case WIMLIB_ERR_SET_REPARSE_DATA:
        case WIMLIB_ERR_SET_SECURITY:
        case WIMLIB_ERR_SET_SHORT_NAME:
        case WIMLIB_ERR_SET_TIMESTAMPS:
        case WIMLIB_ERR_FVE_LOCKED_VOLUME:
        case WIMLIB_ERR_UNABLE_TO_READ_CAPTURE_CONFIG:
            return LastError ? LastError : (DWORD)E_FAIL;

        default:
            return ERROR_INVALID_PARAMETER;
    }
}

static DWORD
DismInitWimlib(VOID)
{
    int Result;

    if (DismWimlibReady)
        return ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    Result = wimlib_global_init(0);
    if (Result)
        return DismWimError(Result);
    DismWimlibReady = TRUE;
    return ERROR_SUCCESS;
}

static DWORD
DismCheckPath(PCWSTR Path, BOOL Directory)
{
    DWORD Attributes = GetFileAttributesW(Path);

    if (Attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    if (Directory && !(Attributes & FILE_ATTRIBUTE_DIRECTORY))
        return ERROR_DIRECTORY;
    if (!Directory && (Attributes & FILE_ATTRIBUTE_DIRECTORY))
        return ERROR_ACCESS_DENIED;
    return ERROR_SUCCESS;
}

static DWORD
DismOpenWim(PCWSTR Path, int OpenFlags, BOOL Progress, WIMStruct **Wim)
{
    DWORD Error;
    int Result;

    *Wim = NULL;
    Error = DismCheckPath(Path, FALSE);
    if (Error)
        return Error;
    Error = DismInitWimlib();
    if (Error)
        return Error;
    if (DismHasOption(L"CheckIntegrity"))
        OpenFlags |= WIMLIB_OPEN_FLAG_CHECK_INTEGRITY;
    SetLastError(ERROR_SUCCESS);
    Result = wimlib_open_wim_with_progress(Path, OpenFlags, Wim,
                                           Progress ? DismProgressCallback : NULL, NULL);
    if (Result)
    {
        *Wim = NULL;
        return DismWimError(Result);
    }
    return ERROR_SUCCESS;
}

static DWORD
DismReferenceParts(WIMStruct *Wim)
{
    PCWSTR Pattern = DismGetValue(L"SWMFile");
    int Result;

    if (!Pattern)
        return ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    Result = wimlib_reference_resource_files(Wim, &Pattern, 1,
                                             WIMLIB_REF_FLAG_GLOB_ENABLE |
                                             WIMLIB_REF_FLAG_GLOB_ERR_ON_NOMATCH,
                                             0);
    if (Result == WIMLIB_ERR_GLOB_HAD_NO_MATCHES)
        return ERROR_FILE_NOT_FOUND;
    return DismWimError(Result);
}

static DWORD
DismParseNumber(PCWSTR Text, ULONG *Value)
{
    PWSTR End;
    ULONG Number;

    if (!Text || !iswdigit(*Text))
        return ERROR_INVALID_PARAMETER;
    Number = wcstoul(Text, &End, 10);
    if (*End || Number == 0)
        return ERROR_INVALID_PARAMETER;
    *Value = Number;
    return ERROR_SUCCESS;
}

static DWORD
DismCheckSelection(PCWSTR IndexName, PCWSTR NameName)
{
    ULONG Index;

    if (DismHasOption(IndexName) && DismHasOption(NameName))
        return DismOptionPair(IDS_OPTIONS_CONFLICT, IndexName, NameName);
    if (DismHasOption(IndexName))
        return DismParseNumber(DismGetValue(IndexName), &Index);
    if (DismHasOption(NameName) && !DismGetValue(NameName))
        return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}

static DWORD
DismSelectImage(WIMStruct *Wim, PCWSTR IndexName, PCWSTR NameName, BOOL Inspect, int *Image)
{
    struct wimlib_wim_info Info;
    PCWSTR Name;
    ULONG Index;
    ULONG i;

    wimlib_get_wim_info(Wim, &Info);

    if (DismHasOption(IndexName))
    {
        DismParseNumber(DismGetValue(IndexName), &Index);
        if (Index > Info.image_count)
        {
            if (Inspect)
                return DismFail(DISM_E_IMAGE_NOT_FOUND, IDS_IMAGE_NOT_FOUND);
            return ERROR_INVALID_PARAMETER;
        }
        *Image = (int)Index;
        return ERROR_SUCCESS;
    }

    Name = DismGetValue(NameName);
    if (Name)
    {
        for (i = 1; i <= Info.image_count; i++)
        {
            PCWSTR ImageName = wimlib_get_image_name(Wim, (int)i);

            if (ImageName && !_wcsicmp(ImageName, Name))
            {
                *Image = (int)i;
                return ERROR_SUCCESS;
            }
        }
        if (Inspect)
            return DismFail(DISM_E_IMAGE_NOT_FOUND, IDS_IMAGE_NOT_FOUND);
        return ERROR_NOT_FOUND;
    }

    if (Inspect)
    {
        *Image = WIMLIB_ALL_IMAGES;
        return ERROR_SUCCESS;
    }
    if (Info.image_count == 1)
    {
        *Image = 1;
        return ERROR_SUCCESS;
    }
    return DismOptionPair(IDS_OPTION_REQUIRED, IndexName, NameName);
}

static VOID
DismFormatNumber(ULONGLONG Value, PWSTR Buffer, int Count)
{
    WCHAR Raw[32];
    WCHAR Decimal[8];
    WCHAR Thousand[8];
    WCHAR Grouping[16];
    NUMBERFMTW Format;
    PCWSTR p;

    StringCchPrintfW(Raw, ARRAYSIZE(Raw), L"%I64u", Value);

    RtlZeroMemory(&Format, sizeof(Format));
    if (!GetLocaleInfoW(DismLcid, LOCALE_SDECIMAL, Decimal, ARRAYSIZE(Decimal)))
        StringCchCopyW(Decimal, ARRAYSIZE(Decimal), L".");
    if (!GetLocaleInfoW(DismLcid, LOCALE_STHOUSAND, Thousand, ARRAYSIZE(Thousand)))
        StringCchCopyW(Thousand, ARRAYSIZE(Thousand), L",");
    if (!GetLocaleInfoW(DismLcid, LOCALE_SGROUPING, Grouping, ARRAYSIZE(Grouping)))
        StringCchCopyW(Grouping, ARRAYSIZE(Grouping), L"3;0");
    for (p = Grouping; *p; p++)
    {
        if (iswdigit(*p) && *p != L'0')
            Format.Grouping = Format.Grouping * 10 + (*p - L'0');
    }
    Format.lpDecimalSep = Decimal;
    Format.lpThousandSep = Thousand;

    if (!GetNumberFormatW(DismLcid, 0, Raw, &Format, Buffer, Count))
        StringCchCopyW(Buffer, Count, Raw);
}

static PCWSTR
DismProperty(WIMStruct *Wim, int Image, PCWSTR Name)
{
    PCWSTR Value = wimlib_get_image_property(Wim, Image, Name);

    if (!Value || !*Value)
        return NULL;
    return Value;
}

static VOID
DismPrintProperty(WIMStruct *Wim, int Image, UINT Id, PCWSTR Name)
{
    WCHAR Undefined[32];
    PCWSTR Value = DismProperty(Wim, Image, Name);

    if (!Value)
    {
        DismLoadString(IDS_UNDEFINED, Undefined, ARRAYSIZE(Undefined));
        Value = Undefined;
    }
    ConResPrintf(StdOut, Id, Value);
}

static BOOL
DismGetTimestamp(WIMStruct *Wim, int Image, PCWSTR Name, FILETIME *Time)
{
    WCHAR Path[64];
    PCWSTR High, Low;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s/HIGHPART", Name);
    High = DismProperty(Wim, Image, Path);
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s/LOWPART", Name);
    Low = DismProperty(Wim, Image, Path);
    if (!High || !Low)
        return FALSE;
    Time->dwHighDateTime = wcstoul(High, NULL, 16);
    Time->dwLowDateTime = wcstoul(Low, NULL, 16);
    return TRUE;
}

static VOID
DismPrintTimestamp(WIMStruct *Wim, int Image, UINT Id, PCWSTR Name)
{
    WCHAR Date[64], Clock[64], Text[160];
    SYSTEMTIME Utc, Local;
    FILETIME Time;

    if (!DismGetTimestamp(Wim, Image, Name, &Time) ||
        !FileTimeToSystemTime(&Time, &Utc) ||
        !SystemTimeToTzSpecificLocalTime(NULL, &Utc, &Local) ||
        !GetDateFormatW(DismLcid, DATE_SHORTDATE, &Local, NULL, Date, ARRAYSIZE(Date)) ||
        !GetTimeFormatW(DismLcid, 0, &Local, NULL, Clock, ARRAYSIZE(Clock)))
    {
        DismLoadString(IDS_UNDEFINED, Text, ARRAYSIZE(Text));
    }
    else
    {
        StringCchPrintfW(Text, ARRAYSIZE(Text), L"%s - %s", Date, Clock);
    }
    ConResPrintf(StdOut, Id, Text);
}

static VOID
DismPrintCount(WIMStruct *Wim, int Image, UINT Id, PCWSTR Name)
{
    WCHAR Number[64];
    PCWSTR Value = DismProperty(Wim, Image, Name);

    StringCchCopyW(Number, ARRAYSIZE(Number), Value ? Value : L"0");
    ConResPrintf(StdOut, Id, Number);
}

static VOID
DismPrintSummary(WIMStruct *Wim, int Image)
{
    WCHAR Size[64];
    PCWSTR Bytes = DismProperty(Wim, Image, L"TOTALBYTES");

    ConResPrintf(StdOut, IDS_INDEX, Image);
    DismPrintProperty(Wim, Image, IDS_NAME, L"NAME");
    DismPrintProperty(Wim, Image, IDS_DESCRIPTION, L"DESCRIPTION");
    DismFormatNumber(Bytes ? _wcstoui64(Bytes, NULL, 10) : 0, Size, ARRAYSIZE(Size));
    ConResPrintf(StdOut, IDS_SIZE, Size);
}

static VOID
DismPrintArchitecture(WIMStruct *Wim, int Image)
{
    WCHAR Text[32];
    PCWSTR Value = DismProperty(Wim, Image, L"WINDOWS/ARCH");

    if (!Value)
    {
        DismPrintProperty(Wim, Image, IDS_ARCHITECTURE, L"WINDOWS/ARCH");
        return;
    }
    switch (wcstoul(Value, NULL, 10))
    {
        case PROCESSOR_ARCHITECTURE_INTEL:
            StringCchCopyW(Text, ARRAYSIZE(Text), L"x86");
            break;
        case PROCESSOR_ARCHITECTURE_ARM:
            StringCchCopyW(Text, ARRAYSIZE(Text), L"arm");
            break;
        case PROCESSOR_ARCHITECTURE_IA64:
            StringCchCopyW(Text, ARRAYSIZE(Text), L"ia64");
            break;
        case PROCESSOR_ARCHITECTURE_AMD64:
            StringCchCopyW(Text, ARRAYSIZE(Text), L"x64");
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            StringCchCopyW(Text, ARRAYSIZE(Text), L"arm64");
            break;
        default:
            StringCchCopyW(Text, ARRAYSIZE(Text), Value);
            break;
    }
    ConResPrintf(StdOut, IDS_ARCHITECTURE, Text);
}

static VOID
DismPrintVersion(WIMStruct *Wim, int Image)
{
    WCHAR Text[64];
    PCWSTR Major = DismProperty(Wim, Image, L"WINDOWS/VERSION/MAJOR");
    PCWSTR Minor = DismProperty(Wim, Image, L"WINDOWS/VERSION/MINOR");
    PCWSTR Build = DismProperty(Wim, Image, L"WINDOWS/VERSION/BUILD");

    if (!Major)
    {
        DismPrintProperty(Wim, Image, IDS_VERSION, L"WINDOWS/VERSION/MAJOR");
        return;
    }
    StringCchPrintfW(Text, ARRAYSIZE(Text), L"%s.%s.%s", Major,
                     Minor ? Minor : L"0", Build ? Build : L"0");
    ConResPrintf(StdOut, IDS_VERSION, Text);
}

static VOID
DismPrintLanguages(WIMStruct *Wim, int Image)
{
    PCWSTR Default = DismProperty(Wim, Image, L"WINDOWS/LANGUAGES/DEFAULT");
    PCWSTR Language;
    WCHAR Path[64];
    ULONG i;

    if (!DismProperty(Wim, Image, L"WINDOWS/LANGUAGES/LANGUAGE"))
    {
        ConResPuts(StdOut, IDS_LANGUAGES_UNDEFINED);
        return;
    }

    ConResPuts(StdOut, IDS_LANGUAGES);
    for (i = 1; ; i++)
    {
        StringCchPrintfW(Path, ARRAYSIZE(Path), L"WINDOWS/LANGUAGES/LANGUAGE[%lu]", i);
        Language = DismProperty(Wim, Image, Path);
        if (!Language)
            break;
        if (Default && !_wcsicmp(Default, Language))
            ConResPrintf(StdOut, IDS_LANGUAGE_DEFAULT, Language);
        else
            ConResPrintf(StdOut, IDS_LANGUAGE, Language);
    }
}

static VOID
DismPrintDetails(WIMStruct *Wim, int Image)
{
    WCHAR Bootable[16];
    PCWSTR WimBoot = DismProperty(Wim, Image, L"WIMBOOT");

    DismPrintSummary(Wim, Image);
    DismLoadString((WimBoot && !wcscmp(WimBoot, L"1")) ? IDS_YES : IDS_NO,
                   Bootable, ARRAYSIZE(Bootable));
    ConResPrintf(StdOut, IDS_WIM_BOOTABLE, Bootable);
    DismPrintArchitecture(Wim, Image);
    DismPrintProperty(Wim, Image, IDS_HAL, L"WINDOWS/HAL");
    DismPrintVersion(Wim, Image);
    DismPrintProperty(Wim, Image, IDS_SP_BUILD, L"WINDOWS/VERSION/SPBUILD");
    DismPrintProperty(Wim, Image, IDS_SP_LEVEL, L"WINDOWS/VERSION/SPLEVEL");
    DismPrintProperty(Wim, Image, IDS_EDITION, L"WINDOWS/EDITIONID");
    DismPrintProperty(Wim, Image, IDS_INSTALLATION, L"WINDOWS/INSTALLATIONTYPE");
    DismPrintProperty(Wim, Image, IDS_PRODUCT_TYPE, L"WINDOWS/PRODUCTTYPE");
    DismPrintProperty(Wim, Image, IDS_PRODUCT_SUITE, L"WINDOWS/PRODUCTSUITE");
    DismPrintProperty(Wim, Image, IDS_SYSTEM_ROOT, L"WINDOWS/SYSTEMROOT");
    DismPrintCount(Wim, Image, IDS_DIRECTORIES, L"DIRCOUNT");
    DismPrintCount(Wim, Image, IDS_FILES, L"FILECOUNT");
    DismPrintTimestamp(Wim, Image, IDS_CREATED, L"CREATIONTIME");
    DismPrintTimestamp(Wim, Image, IDS_MODIFIED, L"LASTMODIFICATIONTIME");
    DismPrintLanguages(Wim, Image);
}

static DWORD
DismShowImageInfo(PCWSTR FileOption, BOOL WimInfo)
{
    struct wimlib_wim_info Info;
    PCWSTR File = DismGetValue(FileOption);
    WIMStruct *Wim;
    DWORD Error;
    int Image;
    ULONG i;

    if (!File)
        return DismMissingOption(FileOption);
    Error = DismCheckSelection(L"Index", L"Name");
    if (Error)
        return Error;
    Error = DismOpenWim(File, 0, FALSE, &Wim);
    if (Error)
        return Error;
    Error = DismSelectImage(Wim, L"Index", L"Name", TRUE, &Image);
    if (Error)
        goto Done;

    if (DismQuiet)
        goto Done;

    ConResPrintf(StdOut, IDS_DETAILS, File);
    if (Image == WIMLIB_ALL_IMAGES)
    {
        wimlib_get_wim_info(Wim, &Info);
        for (i = 1; i <= Info.image_count; i++)
        {
            DismPrintSummary(Wim, (int)i);
            ConPuts(StdOut, L"\n");
        }
    }
    else
    {
        DismPrintDetails(Wim, Image);
        if (WimInfo)
            ConPuts(StdOut, L"\n");
    }

Done:
    wimlib_free(Wim);
    return Error;
}

static DWORD
DismGetImageInfo(VOID)
{
    return DismShowImageInfo(L"ImageFile", FALSE);
}

static DWORD
DismGetWimInfo(VOID)
{
    return DismShowImageInfo(L"WimFile", TRUE);
}

static int
DismListCallback(const struct wimlib_dir_entry *Entry, void *Context)
{
    UNREFERENCED_PARAMETER(Context);

    if (Entry->depth == 0)
        ConPuts(StdOut, L"\\\n");
    else if (Entry->attributes & WIMLIB_FILE_ATTRIBUTE_DIRECTORY)
        ConPrintf(StdOut, L"%s\\\n", Entry->full_path);
    else
        ConPrintf(StdOut, L"%s\n", Entry->full_path);
    return 0;
}

static DWORD
DismListImage(VOID)
{
    PCWSTR File = DismGetValue(L"ImageFile");
    WIMStruct *Wim;
    DWORD Error;
    int Image;
    int Result;

    if (!File)
        return DismMissingOption(L"ImageFile");
    Error = DismCheckSelection(L"Index", L"Name");
    if (Error)
        return Error;
    Error = DismOpenWim(File, 0, FALSE, &Wim);
    if (Error)
        return Error;
    Error = DismSelectImage(Wim, L"Index", L"Name", FALSE, &Image);
    if (Error || DismQuiet)
        goto Done;

    SetLastError(ERROR_SUCCESS);
    Result = wimlib_iterate_dir_tree(Wim, Image, WIMLIB_WIM_ROOT_PATH,
                                     WIMLIB_ITERATE_DIR_TREE_FLAG_RECURSIVE,
                                     DismListCallback, NULL);
    Error = DismWimError(Result);

Done:
    wimlib_free(Wim);
    return Error;
}

static DWORD
DismApplyImage(VOID)
{
    PCWSTR File = DismGetValue(L"ImageFile");
    PCWSTR Target = DismGetValue(L"ApplyDir");
    WIMStruct *Wim;
    DWORD Error;
    int ExtractFlags = 0;
    int Image;
    int Result;

    if (!File)
        return DismMissingOption(L"ImageFile");
    if (!Target)
        return ERROR_BAD_PATHNAME;
    Error = DismCheckSelection(L"Index", L"Name");
    if (Error)
        return Error;
    Error = DismOpenWim(File, 0, TRUE, &Wim);
    if (Error)
        return Error;
    Error = DismReferenceParts(Wim);
    if (Error)
        goto Done;
    Error = DismSelectImage(Wim, L"Index", L"Name", FALSE, &Image);
    if (Error)
        goto Done;
    Error = DismCheckPath(Target, TRUE);
    if (Error)
        goto Done;

    if (DismHasOption(L"NoRpFix"))
        ExtractFlags |= WIMLIB_EXTRACT_FLAG_NORPFIX;
    if (DismHasOption(L"Compact"))
        ExtractFlags |= WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS4K;
    if (DismHasOption(L"WIMBoot"))
        ExtractFlags |= WIMLIB_EXTRACT_FLAG_WIMBOOT;

    DismLog(L"Applying image %d of %s to %s", Image, File, Target);
    DismBeginProgress(IDS_APPLYING);
    SetLastError(ERROR_SUCCESS);
    Result = wimlib_extract_image(Wim, Image, Target, ExtractFlags);
    Error = DismWimError(Result);
    DismEndProgress(Error == ERROR_SUCCESS);

Done:
    wimlib_free(Wim);
    return Error;
}

static DWORD
DismParseCompression(BOOL AllowRecovery, int *Type, BOOL *Solid)
{
    PCWSTR Value = DismGetValue(L"Compress");

    *Solid = FALSE;
    if (!Value)
        return ERROR_SUCCESS;
    if (!_wcsicmp(Value, L"none"))
        *Type = WIMLIB_COMPRESSION_TYPE_NONE;
    else if (!_wcsicmp(Value, L"fast"))
        *Type = WIMLIB_COMPRESSION_TYPE_XPRESS;
    else if (!_wcsicmp(Value, L"max"))
        *Type = WIMLIB_COMPRESSION_TYPE_LZX;
    else if (AllowRecovery && !_wcsicmp(Value, L"recovery"))
    {
        *Type = WIMLIB_COMPRESSION_TYPE_LZMS;
        *Solid = TRUE;
    }
    else
        return DismFail(ERROR_INVALID_PARAMETER, IDS_INVALID_ARGUMENTS);
    return ERROR_SUCCESS;
}

static int
DismWriteFlags(VOID)
{
    return DismHasOption(L"CheckIntegrity") ? WIMLIB_WRITE_FLAG_CHECK_INTEGRITY : 0;
}

static DWORD
DismSetBootIndex(WIMStruct *Wim, int Image)
{
    struct wimlib_wim_info Info;

    if (!DismHasOption(L"Bootable"))
        return ERROR_SUCCESS;
    RtlZeroMemory(&Info, sizeof(Info));
    Info.boot_index = (uint32_t)Image;
    return DismWimError(wimlib_set_wim_info(Wim, &Info, WIMLIB_CHANGE_BOOT_INDEX));
}

static DWORD
DismAddImage(WIMStruct *Wim, PCWSTR Source, PCWSTR Name, PCWSTR Description)
{
    struct wimlib_wim_info Info;
    PCWSTR Config = DismGetValue(L"ConfigFile");
    DWORD Error;
    int AddFlags = 0;
    int Result;
    int Image;

    if (DismHasOption(L"NoRpFix"))
        AddFlags |= WIMLIB_ADD_FLAG_NORPFIX;
    else
        AddFlags |= WIMLIB_ADD_FLAG_RPFIX;
    if (DismHasOption(L"WIMBoot"))
        AddFlags |= WIMLIB_ADD_FLAG_WIMBOOT;
    if (!Config && !(AddFlags & WIMLIB_ADD_FLAG_WIMBOOT))
        AddFlags |= WIMLIB_ADD_FLAG_WINCONFIG;

    SetLastError(ERROR_SUCCESS);
    Result = wimlib_add_image(Wim, Source, NULL, Config, AddFlags);
    if (Result)
        return DismWimError(Result);

    wimlib_get_wim_info(Wim, &Info);
    Image = (int)Info.image_count;
    Error = DismWimError(wimlib_set_image_property(Wim, Image, L"NAME", Name));
    if (!Error && Description)
        Error = DismWimError(wimlib_set_image_property(Wim, Image, L"DESCRIPTION", Description));
    if (!Error)
        Error = DismSetBootIndex(Wim, Image);
    return Error;
}

static DWORD
DismVerifyWritten(PCWSTR File)
{
    WIMStruct *Wim;
    DWORD Error;

    if (!DismHasOption(L"Verify"))
        return ERROR_SUCCESS;
    Error = DismOpenWim(File, 0, FALSE, &Wim);
    if (Error)
        return Error;
    SetLastError(ERROR_SUCCESS);
    Error = DismWimError(wimlib_verify_wim(Wim, 0));
    wimlib_free(Wim);
    return Error;
}

static DWORD
DismCaptureImage(VOID)
{
    PCWSTR File = DismGetValue(L"ImageFile");
    PCWSTR Source = DismGetValue(L"CaptureDir");
    PCWSTR Name = DismGetValue(L"Name");
    WIMStruct *Wim = NULL;
    int Type = WIMLIB_COMPRESSION_TYPE_XPRESS;
    BOOL Solid;
    DWORD Error;
    int Result;

    if (!File)
        return DismMissingOption(L"ImageFile");
    if (!Source)
        return DismMissingOption(L"CaptureDir");
    if (!Name)
        return DismMissingOption(L"Name");
    Error = DismParseCompression(FALSE, &Type, &Solid);
    if (Error)
        return Error;
    Error = DismCheckPath(Source, TRUE);
    if (Error)
        return Error;
    Error = DismInitWimlib();
    if (Error)
        return Error;

    SetLastError(ERROR_SUCCESS);
    Result = wimlib_create_new_wim((enum wimlib_compression_type)Type, &Wim);
    if (Result)
        return DismWimError(Result);
    wimlib_register_progress_function(Wim, DismProgressCallback, NULL);

    DismLog(L"Capturing %s to %s", Source, File);
    DismBeginProgress(IDS_SAVING);
    Error = DismAddImage(Wim, Source, Name, DismGetValue(L"Description"));
    if (!Error)
    {
        SetLastError(ERROR_SUCCESS);
        Result = wimlib_write(Wim, File, WIMLIB_ALL_IMAGES, DismWriteFlags(), 0);
        Error = DismWimError(Result);
    }
    DismEndProgress(Error == ERROR_SUCCESS);
    wimlib_free(Wim);
    if (!Error)
        Error = DismVerifyWritten(File);
    return Error;
}

static DWORD
DismAppendImage(VOID)
{
    PCWSTR File = DismGetValue(L"ImageFile");
    PCWSTR Source = DismGetValue(L"CaptureDir");
    PCWSTR Name = DismGetValue(L"Name");
    WIMStruct *Wim;
    DWORD Error;
    int Result;

    if (!File)
        return DismMissingOption(L"ImageFile");
    if (!Source)
        return DismMissingOption(L"CaptureDir");
    if (!Name)
        return DismMissingOption(L"Name");
    Error = DismOpenWim(File, WIMLIB_OPEN_FLAG_WRITE_ACCESS, TRUE, &Wim);
    if (Error)
        return Error;
    Error = DismCheckPath(Source, TRUE);
    if (Error)
        goto Done;

    DismLog(L"Appending %s to %s", Source, File);
    DismBeginProgress(IDS_SAVING);
    Error = DismAddImage(Wim, Source, Name, DismGetValue(L"Description"));
    if (!Error)
    {
        SetLastError(ERROR_SUCCESS);
        Result = wimlib_overwrite(Wim, DismWriteFlags(), 0);
        Error = DismWimError(Result);
    }
    DismEndProgress(Error == ERROR_SUCCESS);

Done:
    wimlib_free(Wim);
    if (!Error)
        Error = DismVerifyWritten(File);
    return Error;
}

static DWORD
DismTouchImage(WIMStruct *Wim, int Image)
{
    WCHAR High[16], Low[16];
    FILETIME Now;
    DWORD Error;

    GetSystemTimeAsFileTime(&Now);
    StringCchPrintfW(High, ARRAYSIZE(High), L"0x%08lX", Now.dwHighDateTime);
    StringCchPrintfW(Low, ARRAYSIZE(Low), L"0x%08lX", Now.dwLowDateTime);
    Error = DismWimError(wimlib_set_image_property(Wim, Image, L"LASTMODIFICATIONTIME/HIGHPART", High));
    if (!Error)
        Error = DismWimError(wimlib_set_image_property(Wim, Image, L"LASTMODIFICATIONTIME/LOWPART", Low));
    return Error;
}

static DWORD
DismExportImage(VOID)
{
    PCWSTR Source = DismGetValue(L"SourceImageFile");
    PCWSTR Destination = DismGetValue(L"DestinationImageFile");
    PCWSTR DestinationName = DismGetValue(L"DestinationName");
    struct wimlib_wim_info SourceInfo, DestinationInfo;
    WIMStruct *SourceWim = NULL;
    WIMStruct *DestinationWim = NULL;
    PCWSTR Name, Description;
    BOOL Existing, Solid;
    int Type = WIMLIB_COMPRESSION_TYPE_NONE;
    int WriteFlags;
    DWORD Error;
    int Image;
    int Result;

    if (!Source)
        return DismMissingOption(L"SourceImageFile");
    if (!Destination)
        return DismMissingOption(L"DestinationImageFile");
    Error = DismParseCompression(TRUE, &Type, &Solid);
    if (Error)
        return Error;
    Error = DismCheckSelection(L"SourceIndex", L"SourceName");
    if (Error)
        return Error;
    Error = DismOpenWim(Source, 0, FALSE, &SourceWim);
    if (Error)
        return Error;
    Error = DismReferenceParts(SourceWim);
    if (Error)
        goto Done;
    Error = DismSelectImage(SourceWim, L"SourceIndex", L"SourceName", FALSE, &Image);
    if (Error)
        goto Done;

    WriteFlags = DismWriteFlags();
    Existing = GetFileAttributesW(Destination) != INVALID_FILE_ATTRIBUTES;
    if (Existing)
    {
        Error = DismOpenWim(Destination, WIMLIB_OPEN_FLAG_WRITE_ACCESS, TRUE, &DestinationWim);
        if (Error)
            goto Done;
        wimlib_get_wim_info(DestinationWim, &DestinationInfo);
        if (DestinationInfo.wim_version == DISM_SOLID_WIM_VERSION)
            WriteFlags |= WIMLIB_WRITE_FLAG_SOLID;
    }
    else
    {
        wimlib_get_wim_info(SourceWim, &SourceInfo);
        if (!DismHasOption(L"Compress"))
        {
            Type = SourceInfo.compression_type;
            Solid = SourceInfo.wim_version == DISM_SOLID_WIM_VERSION;
        }
        SetLastError(ERROR_SUCCESS);
        Result = wimlib_create_new_wim((enum wimlib_compression_type)Type, &DestinationWim);
        if (Result)
        {
            Error = DismWimError(Result);
            goto Done;
        }
        wimlib_register_progress_function(DestinationWim, DismProgressCallback, NULL);
        if (Solid)
            WriteFlags |= WIMLIB_WRITE_FLAG_SOLID;
    }

    Name = DestinationName ? DestinationName : wimlib_get_image_name(SourceWim, Image);
    Description = wimlib_get_image_description(SourceWim, Image);

    SetLastError(ERROR_SUCCESS);
    Result = wimlib_export_image(SourceWim, Image, DestinationWim, NULL, NULL,
                                 WIMLIB_EXPORT_FLAG_NO_NAMES | WIMLIB_EXPORT_FLAG_NO_DESCRIPTIONS);
    Error = DismWimError(Result);
    if (Error)
        goto Done;

    wimlib_get_wim_info(DestinationWim, &DestinationInfo);
    Image = (int)DestinationInfo.image_count;
    if (Name && *Name)
        Error = DismWimError(wimlib_set_image_property(DestinationWim, Image, L"NAME", Name));
    if (!Error && Description && *Description)
        Error = DismWimError(wimlib_set_image_property(DestinationWim, Image, L"DESCRIPTION", Description));
    if (!Error)
        Error = DismTouchImage(DestinationWim, Image);
    if (!Error)
        Error = DismSetBootIndex(DestinationWim, Image);
    if (Error)
        goto Done;

    DismLog(L"Exporting %s to %s", Source, Destination);
    DismBeginProgress(IDS_EXPORTING);
    SetLastError(ERROR_SUCCESS);
    if (Existing)
        Result = wimlib_overwrite(DestinationWim, WriteFlags, 0);
    else
        Result = wimlib_write(DestinationWim, Destination, WIMLIB_ALL_IMAGES, WriteFlags, 0);
    Error = DismWimError(Result);
    DismEndProgress(Error == ERROR_SUCCESS);

Done:
    wimlib_free(DestinationWim);
    wimlib_free(SourceWim);
    return Error;
}

static DWORD
DismDeleteImage(VOID)
{
    PCWSTR File = DismGetValue(L"ImageFile");
    struct wimlib_wim_info Info;
    WIMStruct *Wim;
    DWORD Error;
    int Image;
    int Result;

    if (!File)
        return DismMissingOption(L"ImageFile");
    Error = DismCheckSelection(L"Index", L"Name");
    if (Error)
        return Error;
    Error = DismOpenWim(File, WIMLIB_OPEN_FLAG_WRITE_ACCESS, FALSE, &Wim);
    if (Error)
        return Error;
    wimlib_get_wim_info(Wim, &Info);
    if (Info.total_parts > 1 || Info.image_count <= 1)
    {
        Error = ERROR_ACCESS_DENIED;
        goto Done;
    }
    Error = DismSelectImage(Wim, L"Index", L"Name", FALSE, &Image);
    if (Error)
        goto Done;

    DismLog(L"Deleting image %d of %s", Image, File);
    SetLastError(ERROR_SUCCESS);
    Result = wimlib_delete_image(Wim, Image);
    if (!Result)
        Result = wimlib_overwrite(Wim, DismWriteFlags(), 0);
    Error = DismWimError(Result);

Done:
    wimlib_free(Wim);
    return Error;
}

static DWORD
DismSplitImage(VOID)
{
    PCWSTR File = DismGetValue(L"ImageFile");
    PCWSTR Parts = DismGetValue(L"SWMFile");
    WIN32_FILE_ATTRIBUTE_DATA Data;
    ULARGE_INTEGER FileSize;
    ULONGLONG PartSize;
    WIMStruct *Wim;
    ULONG Megabytes;
    DWORD Error;
    int Result;

    if (!File)
        return DismMissingOption(L"ImageFile");
    if (!Parts)
        return DismMissingOption(L"SWMFile");
    if (!DismHasOption(L"FileSize"))
        return DismMissingOption(L"FileSize");
    Error = DismParseNumber(DismGetValue(L"FileSize"), &Megabytes);
    if (Error)
        return Error;
    if (!GetFileAttributesExW(File, GetFileExInfoStandard, &Data))
        return GetLastError();
    FileSize.LowPart = Data.nFileSizeLow;
    FileSize.HighPart = Data.nFileSizeHigh;
    PartSize = (ULONGLONG)Megabytes * 1024 * 1024;
    if (FileSize.QuadPart <= PartSize)
        return ERROR_INVALID_PARAMETER;

    Error = DismOpenWim(File, 0, FALSE, &Wim);
    if (Error)
        return Error;
    DismLog(L"Splitting %s into %s", File, Parts);
    SetLastError(ERROR_SUCCESS);
    Result = wimlib_split(Wim, Parts, PartSize, DismWriteFlags());
    Error = DismWimError(Result);
    wimlib_free(Wim);
    return Error;
}

static DWORD
DismGetMountedImageInfo(VOID)
{
    if (DismQuiet)
        return ERROR_SUCCESS;
    ConResPuts(StdOut, IDS_MOUNTED_IMAGES);
    ConResPuts(StdOut, IDS_NO_MOUNTED_IMAGES);
    return ERROR_SUCCESS;
}

static DWORD
DismCleanupMountpoints(VOID)
{
    return ERROR_SUCCESS;
}

static DWORD
DismNotSupported(VOID)
{
    return ERROR_NOT_SUPPORTED;
}

static DWORD
DismParseArguments(int argc, WCHAR *argv[])
{
    PCWSTR Colon;
    size_t Length;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] != L'/')
        {
            StringCchCopyW(DismErrorArg1, ARRAYSIZE(DismErrorArg1), argv[i]);
            return DismFail(ERROR_INVALID_PARAMETER, IDS_UNRECOGNIZED_TOKEN);
        }
        if (DismOptionCount == DISM_MAX_OPTIONS)
            return ERROR_INVALID_PARAMETER;

        Colon = wcschr(argv[i] + 1, L':');
        Length = Colon ? (size_t)(Colon - (argv[i] + 1)) : wcslen(argv[i] + 1);
        if (Length >= ARRAYSIZE(DismOptions[0].Name))
            Length = ARRAYSIZE(DismOptions[0].Name) - 1;
        StringCchCopyNW(DismOptions[DismOptionCount].Name,
                        ARRAYSIZE(DismOptions[0].Name), argv[i] + 1, Length);
        DismOptions[DismOptionCount].Value = Colon ? Colon + 1 : NULL;
        DismOptionCount++;
    }
    return ERROR_SUCCESS;
}

static const DISM_COMMAND *
DismFindCommand(VOID)
{
    ULONG i, j;

    for (i = 0; i < DismOptionCount; i++)
    {
        for (j = 0; j < ARRAYSIZE(DismCommands); j++)
        {
            if (!_wcsicmp(DismOptions[i].Name, DismCommands[j].Name))
                return &DismCommands[j];
        }
    }
    return NULL;
}

static DWORD
DismCheckOptions(VOID)
{
    ULONG i;

    for (i = 0; i < DismOptionCount; i++)
    {
        PCWSTR Name = DismOptions[i].Name;

        if (DismIsListed(DismGlobalOptions, Name))
            continue;
        if (DismCommand &&
            (!_wcsicmp(DismCommand->Name, Name) || !DismCommand->Options ||
             DismIsListed(DismCommand->Options, Name)))
            continue;
        DismLowerName(DismErrorArg1, ARRAYSIZE(DismErrorArg1), L"", Name);
        return DismFail(ERROR_INVALID_PARAMETER, IDS_UNKNOWN_OPTION);
    }
    return ERROR_SUCCESS;
}

static DWORD
DismFinish(DWORD Error)
{
    if (Error == ERROR_SUCCESS)
    {
        if (!DismQuiet)
            ConMsgPuts(StdOut, FORMAT_MESSAGE_FROM_SYSTEM, NULL, ERROR_SUCCESS, DismLangId);
    }
    else
    {
        DismPrintError(Error);
    }
    DismLog(L"Exit code 0x%08lx", Error);
    return Error;
}

int
wmain(int argc, WCHAR *argv[])
{
    DWORD Error;

    ConInitStdStreams();

    Error = DismParseArguments(argc, argv);
    if (Error)
    {
        DismInitLog();
        return (int)DismFinish(Error);
    }

    if (DismHasOption(L"English"))
    {
        DismLangId = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
        DismLcid = MAKELCID(DismLangId, SORT_DEFAULT);
        ConSetThreadUILanguage(DismLangId);
    }
    DismQuiet = DismHasOption(L"Quiet");
    DismInitLog();
    DismLog(L"DISM.EXE: %s", GetCommandLineW());

    DismPrintHeader();
    DismCommand = DismFindCommand();

    if (DismHasOption(L"?"))
    {
        if (!DismQuiet)
            ConResPuts(StdOut, (DismCommand && DismCommand->HelpId) ? DismCommand->HelpId : IDS_HELP_GENERAL);
        return 0;
    }

    Error = DismCheckOptions();
    if (Error)
        return (int)DismFinish(Error);

    if (!DismCommand)
    {
        if (!DismQuiet)
            ConResPuts(StdOut, IDS_HELP_GENERAL);
        return 0;
    }

    Error = DismCommand->Handler();
    if (DismWimlibReady)
        wimlib_global_cleanup();
    return (int)DismFinish(Error);
}
