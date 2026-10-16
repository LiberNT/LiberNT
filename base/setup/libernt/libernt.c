/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Setup entry point, installation steps and Setup data
 * COPYRIGHT:   Copyright 2008-2010 Matthias Kupfer <mkupfer@reactos.org>
 *              Copyright 2008-2009 Dmitry Chapyshev <dmitry@reactos.org>
 *              Copyright 2018-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "libernt.h"
#include <winnls.h>
#include <tzlib.h>
#include <objbase.h>

#define DELAYIMP_INSECURE_WRITABLE_HOOKS
#include <delayimp.h>

#include "resource.h"

#define NDEBUG
#include <debug.h>

HANDLE ProcessHeap;
SETUPDATA SetupData;

PPARTENTRY InstallPartition = NULL;
#define InstallVolume (InstallPartition->Volume)

PPARTENTRY SystemPartition = NULL;
#define SystemVolume (SystemPartition->Volume)

UI_CONTEXT UiContext;
HCURSOR hWaitCursor;

static WCHAR FormatOutput[256];

typedef struct _COPYCONTEXT
{
    PSETUPDATA pSetupData;
    ULONG TotalOperations;
    ULONG CompletedOperations;
} COPYCONTEXT, *PCOPYCONTEXT;

static VOID
SetItemText(
    _In_ UINT uID,
    ...)
{
    WCHAR Format[256];
    WCHAR Text[512];
    va_list Args;

    Text[0] = UNICODE_NULL;
    if (uID && LoadStringW(SetupData.hInstance, uID, Format, ARRAYSIZE(Format)))
    {
        va_start(Args, uID);
        StringCchVPrintfW(Text, ARRAYSIZE(Text), Format, Args);
        va_end(Args);
    }
    SetWindowTextW(UiContext.hWndItem, Text);
}

static VOID
SetActivity(
    _In_ UINT uID)
{
    SetResText(GetDlgItem(UiContext.hwndDlg, IDC_ACTIVITY), uID);
    SetWindowTextW(UiContext.hWndItem, L"");
}

static VOID
SetMarquee(
    _In_ BOOL Enable)
{
    if (Enable)
    {
        UiContext.dwPbStyle = GetWindowLongPtrW(UiContext.hWndProgress, GWL_STYLE);
        SetWindowLongPtrW(UiContext.hWndProgress, GWL_STYLE, UiContext.dwPbStyle | PBS_MARQUEE);
        SendMessageW(UiContext.hWndProgress, PBM_SETMARQUEE, TRUE, 0);
    }
    else
    {
        SendMessageW(UiContext.hWndProgress, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongPtrW(UiContext.hWndProgress, GWL_STYLE, UiContext.dwPbStyle);
    }
}

static VOID
ReportStepFailure(
    _In_ UINT StepId,
    _In_ ULONG Status)
{
    WCHAR Step[256];
    WCHAR Text[512];

    if (!LoadStringW(SetupData.hInstance, StepId, Step, ARRAYSIZE(Step)))
        Step[0] = UNICODE_NULL;
    LoadPrintfW(Text, ARRAYSIZE(Text), IDS_INSTALL_STEP_FAILED, Step, Status);
    DPRINT1("Setup step %u failed with 0x%08lx\n", StepId, Status);
    SetupMessage(GetParent(UiContext.hwndDlg), TD_ERROR_ICON, TDCBF_CLOSE_BUTTON, IDCLOSE,
                 MAKEINTRESOURCEW(IDS_INSTALL_FAILED), Text);
}

static BOOLEAN
NTAPI
FormatCallback(
    _In_ CALLBACKCOMMAND Command,
    _In_ ULONG Modifier,
    _In_ PVOID Argument)
{
    if (Command == PROGRESS)
        SendMessageW(UiContext.hWndProgress, PBM_SETPOS, *(PULONG)Argument, 0);
    else if (Command == OUTPUT)
        StringCchPrintfW(FormatOutput, ARRAYSIZE(FormatOutput), L"%S", ((PTEXTOUTPUT)Argument)->Output);
    return TRUE;
}

static BOOLEAN
NTAPI
ChkdskCallback(
    _In_ CALLBACKCOMMAND Command,
    _In_ ULONG Modifier,
    _In_ PVOID Argument)
{
    return TRUE;
}

static FSVOL_OP
CALLBACK
FsVolCallback(
    _In_opt_ PVOID Context,
    _In_ FSVOLNOTIFY FormatStatus,
    _In_ ULONG_PTR Param1,
    _In_ ULONG_PTR Param2)
{
    switch (FormatStatus)
    {
        case FSVOLNOTIFY_PARTITIONERROR:
            ReportStepFailure(IDS_PREPARE_PARTITIONS, (ULONG)Param1);
            return FSVOL_ABORT;

        case FSVOLNOTIFY_STARTQUEUE:
        case FSVOLNOTIFY_ENDQUEUE:
            return FSVOL_DOIT;

        case FSVOLNOTIFY_STARTSUBQUEUE:
            SetWindowTextW(UiContext.hWndItem, L"");
            if ((FSVOL_OP)Param1 == FSVOL_CHECK)
                SetMarquee(TRUE);
            return FSVOL_DOIT;

        case FSVOLNOTIFY_ENDSUBQUEUE:
            if ((FSVOL_OP)Param1 == FSVOL_CHECK)
                SetMarquee(FALSE);
            return 0;

        case FSVOLNOTIFY_FORMATERROR:
        {
            PFORMAT_VOLUME_INFO FmtInfo = (PFORMAT_VOLUME_INFO)Param1;
            WCHAR Reason[64];
            WCHAR Text[512];

            if (FormatOutput[0])
                StringCchCopyW(Reason, ARRAYSIZE(Reason), FormatOutput);
            else
                StringCchPrintfW(Reason, ARRAYSIZE(Reason), L"0x%08lx", FmtInfo->ErrorStatus);
            LoadPrintfW(Text, ARRAYSIZE(Text), IDS_FORMAT_FAILED,
                        FmtInfo->Volume->Info.DeviceName,
                        FmtInfo->FileSystemName ? FmtInfo->FileSystemName : L"",
                        Reason);
            SetupMessage(GetParent(UiContext.hwndDlg), TD_ERROR_ICON, TDCBF_CLOSE_BUTTON, IDCLOSE,
                         MAKEINTRESOURCEW(IDS_INSTALL_FAILED), Text);
            return FSVOL_ABORT;
        }

        case FSVOLNOTIFY_CHECKERROR:
        {
            PCHECK_VOLUME_INFO ChkInfo = (PCHECK_VOLUME_INFO)Param1;
            WCHAR Text[512];

            if (ChkInfo->ErrorStatus != STATUS_NOT_SUPPORTED)
            {
                LoadPrintfW(Text, ARRAYSIZE(Text), IDS_CHECK_FAILED,
                            ChkInfo->Volume->Info.DeviceName, ChkInfo->ErrorStatus);
                SetupMessage(GetParent(UiContext.hwndDlg), TD_WARNING_ICON, TDCBF_OK_BUTTON, IDOK,
                             NULL, Text);
            }
            return FSVOL_SKIP;
        }

        case FSVOLNOTIFY_STARTFORMAT:
        {
            PFORMAT_VOLUME_INFO FmtInfo = (PFORMAT_VOLUME_INFO)Param1;
            PVOL_CREATE_INFO VolCreate = FindVolumeFormatPlan(FmtInfo->Volume);

            if (!VolCreate || !*VolCreate->FileSystemName)
                return FSVOL_SKIP;

            if (FmtInfo->Volume->Info.DriveLetter)
            {
                SetItemText(IDS_FORMATTING_PROGRESS1, FmtInfo->Volume->Info.DriveLetter,
                            FmtInfo->Volume->Info.DeviceName, VolCreate->FileSystemName);
            }
            else
            {
                SetItemText(IDS_FORMATTING_PROGRESS2, FmtInfo->Volume->Info.DeviceName,
                            VolCreate->FileSystemName);
            }

            FormatOutput[0] = UNICODE_NULL;
            FmtInfo->FileSystemName = VolCreate->FileSystemName;
            FmtInfo->MediaFlag = VolCreate->MediaFlag;
            FmtInfo->Label = VolCreate->Label;
            FmtInfo->QuickFormat = VolCreate->QuickFormat;
            FmtInfo->ClusterSize = VolCreate->ClusterSize;
            FmtInfo->Callback = FormatCallback;

            SendMessageW(UiContext.hWndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageW(UiContext.hWndProgress, PBM_SETPOS, 0, 0);
            return FSVOL_DOIT;
        }

        case FSVOLNOTIFY_ENDFORMAT:
        {
            PFORMAT_VOLUME_INFO FmtInfo = (PFORMAT_VOLUME_INFO)Param1;

            if (FmtInfo->FileSystemName)
                *(PWSTR)FmtInfo->FileSystemName = UNICODE_NULL;
            return 0;
        }

        case FSVOLNOTIFY_STARTCHECK:
        {
            PCHECK_VOLUME_INFO ChkInfo = (PCHECK_VOLUME_INFO)Param1;

            if (!FindVolumeFormatPlan(ChkInfo->Volume))
                return FSVOL_SKIP;

            if (ChkInfo->Volume->Info.DriveLetter)
            {
                SetItemText(IDS_CHECKING_PROGRESS1, ChkInfo->Volume->Info.DriveLetter,
                            ChkInfo->Volume->Info.DeviceName);
            }
            else
            {
                SetItemText(IDS_CHECKING_PROGRESS2, ChkInfo->Volume->Info.DeviceName);
            }

            ChkInfo->FixErrors = TRUE;
            ChkInfo->Verbose = FALSE;
            ChkInfo->CheckOnlyIfDirty = TRUE;
            ChkInfo->ScanDrive = FALSE;
            ChkInfo->Callback = ChkdskCallback;
            return FSVOL_DOIT;
        }

        case ChangeSystemPartition:
            return FSVOL_ABORT;

        default:
            return 0;
    }
}

static PCWSTR
GetFileName(
    _In_ PCWSTR Path)
{
    PCWSTR Name = wcsrchr(Path, L'\\');

    return Name ? Name + 1 : Path;
}

static UINT
CALLBACK
FileCopyCallback(
    PVOID Context,
    UINT Notification,
    UINT_PTR Param1,
    UINT_PTR Param2)
{
    PCOPYCONTEXT CopyContext = (PCOPYCONTEXT)Context;
    PFILEPATHS_W FilePathInfo = (PFILEPATHS_W)Param1;

    WaitForSingleObject(CopyContext->pSetupData->hHaltInstallEvent, INFINITE);
    if (CopyContext->pSetupData->bAbortInstall)
        return FILEOP_ABORT;

    switch (Notification)
    {
        case SPFILENOTIFY_STARTSUBQUEUE:
            CopyContext->TotalOperations = (ULONG)Param2;
            CopyContext->CompletedOperations = 0;
            SendMessageW(UiContext.hWndProgress, PBM_SETRANGE32, 0, CopyContext->TotalOperations);
            SendMessageW(UiContext.hWndProgress, PBM_SETSTEP, 1, 0);
            SendMessageW(UiContext.hWndProgress, PBM_SETPOS, 0, 0);
            break;

        case SPFILENOTIFY_STARTDELETE:
            SetItemText(IDS_DELETING, GetFileName(FilePathInfo->Target));
            break;

        case SPFILENOTIFY_STARTRENAME:
        {
            PCWSTR Source = GetFileName(FilePathInfo->Source);
            PCWSTR Target = GetFileName(FilePathInfo->Target);

            SetItemText(_wcsicmp(Source, Target) ? IDS_RENAMING : IDS_MOVING, Source, Target);
            break;
        }

        case SPFILENOTIFY_STARTCOPY:
            SetItemText(IDS_COPYING, GetFileName(FilePathInfo->Target));
            break;

        case SPFILENOTIFY_COPYERROR:
            DPRINT1("Skipping '%S' after copy error %lu\n", FilePathInfo->Target, FilePathInfo->Win32Error);
            return FILEOP_SKIP;

        case SPFILENOTIFY_ENDDELETE:
        case SPFILENOTIFY_ENDRENAME:
        case SPFILENOTIFY_ENDCOPY:
            CopyContext->CompletedOperations++;
            if (CopyContext->TotalOperations >> 1 == CopyContext->CompletedOperations)
                DPRINT1("CHECKPOINT:HALF_COPIED\n");
            SendMessageW(UiContext.hWndProgress, PBM_STEPIT, 0, 0);
            break;
    }

    return FILEOP_DOIT;
}

static VOID
__cdecl
RegistryStatus(IN REGISTRY_STATUS RegStatus, ...)
{
    static const UINT StringIDs[] =
    {
        IDS_REG_DONE,
        IDS_REG_REGHIVEUPDATE,
        IDS_REG_IMPORTFILE,
        IDS_REG_DISPLAYSETTINGSUPDATE,
        IDS_REG_LOCALESETTINGSUPDATE,
        IDS_REG_ADDKBLAYOUTS,
        IDS_REG_KEYBOARDSETTINGSUPDATE,
        IDS_REG_CODEPAGEINFOUPDATE,
    };
    WCHAR Format[256];
    WCHAR Text[512];
    va_list Args;

    Text[0] = UNICODE_NULL;
    if (RegStatus < ARRAYSIZE(StringIDs) &&
        LoadStringW(SetupData.hInstance, StringIDs[RegStatus], Format, ARRAYSIZE(Format)))
    {
        va_start(Args, RegStatus);
        StringCchVPrintfW(Text, ARRAYSIZE(Text), Format, Args);
        va_end(Args);
    }
    SetWindowTextW(UiContext.hWndItem, Text);
    SendMessageW(UiContext.hWndProgress, PBM_STEPIT, 0, 0);
}

static VOID
ShowConfigureProgress(
    _In_ PCWSTR Line)
{
    ULONG Phase;

    if (Line[0] == L'#')
    {
        Phase = wcstoul(&Line[1], NULL, 10);
        if (Phase >= 1 && Phase <= IDS_CONFIG_PHASE_LAST - IDS_CONFIG_PHASE_FIRST + 1)
        {
            SetResText(UiContext.hWndItem, IDS_CONFIG_PHASE_FIRST + Phase - 1);
            SendMessageW(UiContext.hWndProgress, PBM_SETPOS, Phase, 0);
        }
    }
    else
    {
        SetWindowTextW(UiContext.hWndItem, Line);
    }
}

static DWORD
RunTargetConfiguration(
    _In_ PCWSTR Settings)
{
    SECURITY_ATTRIBUTES SecurityAttributes = { sizeof(SecurityAttributes), NULL, TRUE };
    HANDLE hInputRead = NULL, hInputWrite = NULL;
    HANDLE hOutputRead = NULL, hOutputWrite = NULL;
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    WCHAR ApplicationName[MAX_PATH];
    WCHAR CommandLine[MAX_PATH + 16];
    WCHAR Output[512];
    DWORD cbOutput = 0, cbRead, Written, ExitCode = ERROR_GEN_FAILURE;
    PWCHAR LineEnd;
    BOOL Success;

    if (!GetSystemDirectoryW(ApplicationName, ARRAYSIZE(ApplicationName)) ||
        FAILED(StringCchCatW(ApplicationName, ARRAYSIZE(ApplicationName), L"\\setup.exe")) ||
        FAILED(StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" -target", ApplicationName)))
    {
        return ERROR_BUFFER_OVERFLOW;
    }

    if (!CreatePipe(&hInputRead, &hInputWrite, &SecurityAttributes, 0) ||
        !SetHandleInformation(hInputWrite, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&hOutputRead, &hOutputWrite, &SecurityAttributes, 0) ||
        !SetHandleInformation(hOutputRead, HANDLE_FLAG_INHERIT, 0))
    {
        ExitCode = GetLastError();
        goto done;
    }

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = hInputRead;
    StartupInfo.hStdOutput = hOutputWrite;
    StartupInfo.hStdError = hOutputWrite;

    Success = CreateProcessW(ApplicationName, CommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                             NULL, NULL, &StartupInfo, &ProcessInfo);
    CloseHandle(hInputRead);
    hInputRead = NULL;
    CloseHandle(hOutputWrite);
    hOutputWrite = NULL;
    if (!Success)
    {
        ExitCode = GetLastError();
        goto done;
    }

    WriteFile(hInputWrite, Settings, (DWORD)(wcslen(Settings) * sizeof(WCHAR)), &Written, NULL);
    CloseHandle(hInputWrite);
    hInputWrite = NULL;

    while (ReadFile(hOutputRead, (PBYTE)Output + cbOutput, sizeof(Output) - sizeof(WCHAR) - cbOutput, &cbRead, NULL) &&
           cbRead != 0)
    {
        cbOutput += cbRead;
        Output[cbOutput / sizeof(WCHAR)] = UNICODE_NULL;

        while ((LineEnd = wcschr(Output, L'\n')) != NULL)
        {
            *LineEnd = UNICODE_NULL;
            ShowConfigureProgress(Output);
            cbOutput -= (DWORD)((LineEnd + 1 - Output) * sizeof(WCHAR));
            MoveMemory(Output, LineEnd + 1, cbOutput + sizeof(WCHAR));
        }

        if (cbOutput >= sizeof(Output) - sizeof(WCHAR))
            cbOutput = 0;
    }

    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
    if (!GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode))
        ExitCode = GetLastError();
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);

done:
    if (hInputRead)
        CloseHandle(hInputRead);
    if (hInputWrite)
        CloseHandle(hInputWrite);
    if (hOutputRead)
        CloseHandle(hOutputRead);
    if (hOutputWrite)
        CloseHandle(hOutputWrite);

    return ExitCode;
}

static DWORD
ConfigureInstalledSystem(
    _In_ PSETUPDATA pSetupData)
{
    WCHAR SystemRoot[MAX_PATH];
    PWSTR Settings;
    SIZE_T cchSettings = 4096;
    SIZE_T cchKey;
    PCWSTR Directory = pSetupData->USetupData.InstallationDirectory;
    NTSTATUS Status;
    DWORD Error;

    StringCchPrintfW(SystemRoot, ARRAYSIZE(SystemRoot), L"C:%s%s",
                     (Directory[0] == L'\\') ? L"" : L"\\", Directory);
    cchKey = wcslen(SystemRoot);
    if (cchKey > 3 && SystemRoot[cchKey - 1] == L'\\')
        SystemRoot[cchKey - 1] = UNICODE_NULL;

    Settings = HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, cchSettings * sizeof(WCHAR));
    if (!Settings)
        return ERROR_NOT_ENOUGH_MEMORY;

    if (FAILED(StringCchPrintfW(Settings, cchSettings,
                                L"MachineKey=%.*s\n"
                                L"UserKey=%.*s\n"
                                L"SystemVolume=%s\n"
                                L"SystemDrive=C:\n"
                                L"SystemRoot=%s\n"
                                L"ComputerName=%s\n"
                                L"UserName=%s\n"
                                L"Password=%s\n"
                                L"TimeZoneIndex=%lu\n"
                                L"AutoDaylight=%u\n",
                                (int)(wcslen(REGISTRY_SETUP_MACHINE) - 1), REGISTRY_SETUP_MACHINE,
                                (int)(wcslen(REGISTRY_SETUP_USER) - 1), REGISTRY_SETUP_USER,
                                InstallVolume->Info.DeviceName,
                                SystemRoot,
                                pSetupData->ComputerName,
                                pSetupData->UserName,
                                pSetupData->Password,
                                pSetupData->TimeZoneIndex,
                                pSetupData->AutoDaylight ? 1 : 0)))
    {
        HeapFree(ProcessHeap, 0, Settings);
        return ERROR_BUFFER_OVERFLOW;
    }

    Status = MountTargetRegistry(&pSetupData->USetupData);
    if (NT_SUCCESS(Status))
    {
        Error = RunTargetConfiguration(Settings);
        UnmountTargetRegistry();
    }
    else
    {
        Error = RtlNtStatusToDosError(Status);
    }

    SecureZeroMemory(Settings, cchSettings * sizeof(WCHAR));
    HeapFree(ProcessHeap, 0, Settings);
    SecureZeroMemory(pSetupData->Password, sizeof(pSetupData->Password));

    return Error;
}

static DWORD
WINAPI
PrepareAndDoCopyThread(
    IN LPVOID Param)
{
    HWND hwndDlg = (HWND)Param;
    PSETUPDATA pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);
    WCHAR PathBuffer[RTL_NUMBER_OF_FIELD(PARTENTRY, DeviceName) + 1];
    COPYCONTEXT CopyContext;
    ERROR_NUMBER ErrorNumber;
    BOOLEAN Success = FALSE;
    NTSTATUS Status;
    DWORD Error;

    UiContext.hwndDlg = hwndDlg;
    UiContext.hWndItem = GetDlgItem(hwndDlg, IDC_ITEM);
    UiContext.hWndProgress = GetDlgItem(hwndDlg, IDC_PROCESSPROGRESS);
    UiContext.dwPbStyle = 0;

    EnableSetupCancel(GetParent(hwndDlg), FALSE);

    SetActivity(IDS_CONFIG_SYSTEM_PARTITION);
    if (!InitSystemPartition(pSetupData->PartitionList, InstallPartition, &SystemPartition,
                             FsVolCallback, NULL))
    {
        ReportStepFailure(IDS_CONFIG_SYSTEM_PARTITION, (ULONG)STATUS_UNSUCCESSFUL);
        goto Quit;
    }

    SetActivity(IDS_PREPARE_PARTITIONS);
    if (!FsVolCommitOpsQueue(pSetupData->PartitionList, SystemVolume, InstallVolume,
                             FsVolCallback, NULL))
    {
        goto Quit;
    }

    EnableSetupCancel(GetParent(hwndDlg), TRUE);

    Status = InitDestinationPaths(&pSetupData->USetupData, NULL, InstallVolume);
    if (!NT_SUCCESS(Status))
    {
        ReportStepFailure(IDS_PREPARE_FILES, Status);
        goto Quit;
    }

    SetActivity(IDS_PREPARE_FILES);
    SetMarquee(TRUE);
    Success = PrepareFileCopy(&pSetupData->USetupData, NULL);
    SetMarquee(FALSE);
    if (!Success)
    {
        if (!pSetupData->bAbortInstall)
            ReportStepFailure(IDS_PREPARE_FILES, (ULONG)STATUS_UNSUCCESSFUL);
        goto Quit;
    }

    SetActivity(IDS_COPYING_FILES);
    CopyContext.pSetupData = pSetupData;
    CopyContext.TotalOperations = 0;
    CopyContext.CompletedOperations = 0;
    Success = DoFileCopy(&pSetupData->USetupData, FileCopyCallback, &CopyContext);
    if (!Success)
    {
        if (!pSetupData->bAbortInstall)
            ReportStepFailure(IDS_COPYING_FILES, (ULONG)STATUS_UNSUCCESSFUL);
        goto Quit;
    }

    EnableSetupCancel(GetParent(hwndDlg), FALSE);
    Success = FALSE;

    SetActivity(IDS_CREATE_REGISTRY);
    SendMessageW(UiContext.hWndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 8));
    SendMessageW(UiContext.hWndProgress, PBM_SETSTEP, 1, 0);
    SendMessageW(UiContext.hWndProgress, PBM_SETPOS, 0, 0);
    ErrorNumber = UpdateRegistry(&pSetupData->USetupData,
                                 FALSE,
                                 pSetupData->PartitionList,
                                 InstallVolume->Info.DriveLetter,
                                 pSetupData->SelectedLanguageId,
                                 RegistryStatus,
                                 NULL);
    if (ErrorNumber != NOT_AN_ERROR)
    {
        ReportStepFailure(IDS_CREATE_REGISTRY, (ULONG)ErrorNumber);
        goto Quit;
    }

    SetActivity(IDS_CONFIGURE_SYSTEM);
    SendMessageW(UiContext.hWndProgress, PBM_SETRANGE, 0,
                 MAKELPARAM(0, IDS_CONFIG_PHASE_LAST - IDS_CONFIG_PHASE_FIRST + 1));
    SendMessageW(UiContext.hWndProgress, PBM_SETPOS, 0, 0);
    Error = ConfigureInstalledSystem(pSetupData);
    if (Error != ERROR_SUCCESS)
    {
        ReportStepFailure(IDS_CONFIGURE_SYSTEM, Error);
        goto Quit;
    }

    SetActivity(IDS_INSTALL_BOOTLOADER);
    if (pSetupData->USetupData.BootLoaderLocation == 2 ||
        pSetupData->USetupData.BootLoaderLocation == 3)
    {
        RtlFreeUnicodeString(&pSetupData->USetupData.SystemRootPath);
        StringCchPrintfW(PathBuffer, ARRAYSIZE(PathBuffer), L"%s\\", SystemPartition->DeviceName);
        RtlCreateUnicodeString(&pSetupData->USetupData.SystemRootPath, PathBuffer);

        Status = InstallBootManagerAndBootEntries(pSetupData->USetupData.ArchType,
                                                  &pSetupData->USetupData.SystemRootPath,
                                                  &pSetupData->USetupData.SourceRootPath,
                                                  &pSetupData->USetupData.DestinationArcPath,
                                                  (pSetupData->USetupData.BootLoaderLocation == 2) ? 1 : 0);
        if (Status != STATUS_SUCCESS)
        {
            ReportStepFailure(IDS_INSTALL_BOOTLOADER, Status);
            goto Quit;
        }
    }

    Success = TRUE;

Quit:
    PostMessageW(hwndDlg, PM_INSTALL_DONE, Success, 0);
    return Success;
}

INT_PTR
CALLBACK
InstallPageProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PSETUPDATA pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pSetupData = (PSETUPDATA)lParam;
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (DWORD_PTR)pSetupData);
            SetDlgItemTextW(hwndDlg, IDC_ACTIVITY, L"");
            SetDlgItemTextW(hwndDlg, IDC_ITEM, L"");
            break;

        case WM_SETCURSOR:
            if (!hWaitCursor)
                break;
            SetCursor(hWaitCursor);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, TRUE);
            return TRUE;

        case SPN_SETACTIVE:
            SetupFrameShowButtons(hwndDlg, FALSE, FALSE, TRUE);
            PostMessageW(hwndDlg, PM_INSTALL_START, 0, 0);
            return TRUE;

        case SPN_KILLACTIVE:
        {
            DWORD dwStatus;

            if (InterlockedFlagsTestAndSet8(&pSetupData->bStopInstall, SETUP_PAGE_SWITCHING))
                return TRUE;
            if (!pSetupData->hInstallThread)
                return TRUE;

            dwStatus = WaitForSingleObject(pSetupData->hInstallThread, 0);
            if (dwStatus == WAIT_TIMEOUT)
            {
                HCURSOR hOldCursor;

                hWaitCursor = LoadCursor(NULL, IDC_WAIT);
                hOldCursor = SetCursor(hWaitCursor);
                ShowCursor(TRUE);

                for (;;)
                {
                    MSG msg;

                    dwStatus = MsgWaitForMultipleObjects(1, &pSetupData->hInstallThread, FALSE, INFINITE,
                                                         QS_ALLINPUT | QS_ALLPOSTMESSAGE);
                    if (dwStatus != (WAIT_OBJECT_0 + 1))
                        break;

                    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                    {
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }
                }

                ShowCursor(FALSE);
                SetCursor(hOldCursor);
                hWaitCursor = NULL;
            }

            CloseHandle(pSetupData->hInstallThread);
            pSetupData->hInstallThread = NULL;
            CloseHandle(pSetupData->hHaltInstallEvent);
            pSetupData->hHaltInstallEvent = NULL;
            return TRUE;
        }

        case SPN_QUERYCANCEL:
        {
            INT nRet;

            if (pSetupData->bStopInstall)
            {
                nRet = IDYES;
            }
            else
            {
                ResetEvent(pSetupData->hHaltInstallEvent);
                nRet = SetupMessage(GetParent(hwndDlg), TD_WARNING_ICON,
                                    TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, IDNO,
                                    MAKEINTRESOURCEW(IDS_ABORTSETUP2), MAKEINTRESOURCEW(IDS_ABORTSETUP));
                if (nRet == IDYES)
                    InterlockedOr8((PCHAR)&pSetupData->bStopInstall, SETUP_ABORT_INSTALL);
                SetEvent(pSetupData->hHaltInstallEvent);
            }

            if ((nRet == IDYES) && !pSetupData->bPageSwitching &&
                !InterlockedFlagsTestAndSet8(&pSetupData->bStopInstall, SETUP_IS_CANCELLING))
            {
                PostMessageW(hwndDlg, PM_INSTALL_DONE, FALSE, 0);
            }

            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }

        case SPN_WIZBACK:
        case SPN_WIZNEXT:
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
            return TRUE;

        case PM_INSTALL_START:
        {
            HWND hWndParent = GetParent(hwndDlg);

            ASSERT(pSetupData->hInstallThread == NULL);
            InvalidateRect(hWndParent, NULL, FALSE);
            UpdateWindow(hWndParent);

            pSetupData->bStopInstall = FALSE;
            pSetupData->bInstalled = FALSE;
            pSetupData->hHaltInstallEvent = CreateEventW(NULL, TRUE, TRUE, NULL);
            if (pSetupData->hHaltInstallEvent)
            {
                pSetupData->hInstallThread = CreateThread(NULL, 0, PrepareAndDoCopyThread,
                                                          (PVOID)hwndDlg, 0, NULL);
            }
            if (!pSetupData->hInstallThread)
            {
                if (pSetupData->hHaltInstallEvent)
                    CloseHandle(pSetupData->hHaltInstallEvent);
                pSetupData->hHaltInstallEvent = NULL;
                InterlockedOr8((PCHAR)&pSetupData->bStopInstall, SETUP_ABORT_INSTALL);
                SendMessageW(hwndDlg, PM_INSTALL_DONE, FALSE, 0);
            }
            break;
        }

        case PM_INSTALL_DONE:
        {
            HWND hWndParent = GetParent(hwndDlg);
            BOOL Success = !!wParam;

            if (!Success && !pSetupData->bStopInstall)
            {
                EnableSetupCancel(hWndParent, TRUE);
                InterlockedOr8((PCHAR)&pSetupData->bStopInstall, SETUP_ABORT_INSTALL);
                break;
            }

            EnableSetupCancel(hWndParent, FALSE);

            if (!Success)
                InterlockedOr8((PCHAR)&pSetupData->bStopInstall, SETUP_ABORT_INSTALL);
            else if (pSetupData->bAbortInstall)
                Success = FALSE;

            if (!Success)
                InterlockedOr8((PCHAR)&pSetupData->bStopInstall, SETUP_IS_CANCELLING);
            pSetupData->bInstalled = Success;
            SetupFrameGoTo(hwndDlg, SETUP_PAGE_DONE);
            break;
        }

        default:
            break;
    }

    return FALSE;
}

static VOID
LoadUnattendedAccountSettings(
    _Inout_ PSETUPDATA pSetupData)
{
    WCHAR Path[MAX_PATH];
    WCHAR Value[MAX_PATH];
    PWSTR Name;
    DWORD Length;

    Length = GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    if (Length == 0 || Length >= ARRAYSIZE(Path) || !(Name = wcsrchr(Path, L'\\')))
        return;
    *Name = UNICODE_NULL;
    if (FAILED(StringCchCatW(Path, ARRAYSIZE(Path), L"\\unattend.inf")))
        return;

    if (GetPrivateProfileStringW(L"Unattend", L"ComputerName", L"", Value, ARRAYSIZE(Value), Path) &&
        IsValidSetupComputerName(Value))
    {
        CharUpperW(Value);
        StringCchCopyW(pSetupData->ComputerName, ARRAYSIZE(pSetupData->ComputerName), Value);
    }

    GetPrivateProfileStringW(L"Unattend", L"UserName", L"User", Value, ARRAYSIZE(Value), Path);
    if (IsValidSetupUserName(Value))
        StringCchCopyW(pSetupData->UserName, ARRAYSIZE(pSetupData->UserName), Value);

    GetPrivateProfileStringW(L"Unattend", L"UserPassword", L"",
                             pSetupData->Password, ARRAYSIZE(pSetupData->Password), Path);

    pSetupData->TimeZoneIndex = GetPrivateProfileIntW(L"Unattend", L"TimeZoneIndex",
                                                      pSetupData->TimeZoneIndex, Path);
    pSetupData->AutoDaylight = !GetPrivateProfileIntW(L"Unattend", L"DisableAutoDaylightTimeSet", 0, Path);
}

static BOOL
SelectListEntryById(
    _In_opt_ PGENERIC_LIST List,
    _In_ PCWSTR Id)
{
    PGENERIC_LIST_ENTRY Entry;

    if (!List)
        return FALSE;

    for (Entry = GetFirstListEntry(List); Entry; Entry = GetNextListEntry(Entry))
    {
        PGENENTRY Data = (PGENENTRY)GetListEntryData(Entry);

        if (Data && Data->Id && _wcsicmp(Data->Id, Id) == 0)
        {
            SetCurrentListEntry(List, Entry);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
LoadSetupData(
    _Inout_ PSETUPDATA pSetupData)
{
    pSetupData->PartitionList = CreatePartitionList();
    if (!pSetupData->PartitionList)
    {
        DPRINT1("Could not enumerate available disks; failing installation\n");
        return FALSE;
    }

    pSetupData->USetupData.ComputerList = CreateComputerTypeList(pSetupData->USetupData.SetupInf);
    pSetupData->USetupData.DisplayList = CreateDisplayDriverList(pSetupData->USetupData.SetupInf);
    pSetupData->USetupData.KeyboardList = CreateKeyboardDriverList(pSetupData->USetupData.SetupInf);
    pSetupData->USetupData.LanguageList = CreateLanguageList(pSetupData->USetupData.SetupInf,
                                                             pSetupData->DefaultLanguage);

    if (!pSetupData->bUnattend)
    {
        StringCchPrintfW(pSetupData->USetupData.LocaleID, ARRAYSIZE(pSetupData->USetupData.LocaleID),
                         L"%08lx", GetUserDefaultLCID());
    }

    StringCchCopyW(pSetupData->DefaultLanguage, ARRAYSIZE(pSetupData->DefaultLanguage),
                   pSetupData->USetupData.LocaleID);
    pSetupData->SelectedLanguageId = pSetupData->DefaultLanguage;
    pSetupData->USetupData.LanguageId = (LANGID)(wcstoul(pSetupData->SelectedLanguageId, NULL, 16) & 0xFFFF);

    pSetupData->USetupData.LayoutList = CreateKeyboardLayoutList(pSetupData->USetupData.SetupInf,
                                                                 pSetupData->SelectedLanguageId,
                                                                 pSetupData->DefaultKBLayout);
    if (!pSetupData->bUnattend)
        GetKeyboardLayoutNameW(pSetupData->DefaultKBLayout);

    GenerateComputerName(pSetupData->ComputerName);
    pSetupData->TimeZoneIndex = (ULONG)-1;
    GetTimeZoneListIndex(&pSetupData->TimeZoneIndex);
    pSetupData->AutoDaylight = TRUE;
    if (pSetupData->bUnattend)
        LoadUnattendedAccountSettings(pSetupData);

    SelectListEntryById(pSetupData->USetupData.LanguageList, pSetupData->DefaultLanguage);
    SelectListEntryById(pSetupData->USetupData.LayoutList,
                        pSetupData->bUnattend ? pSetupData->DefaultLanguage : pSetupData->DefaultKBLayout);

    return TRUE;
}

static ULONG
CreateChoicesFromList(
    _In_opt_ PGENERIC_LIST List,
    _In_ BOOL LocaleNames,
    _Out_ PSETUP_CHOICE* Choices)
{
    PGENERIC_LIST_ENTRY Entry;
    PSETUP_CHOICE Items;
    ULONG Total, Count = 0, Index;

    *Choices = NULL;
    Total = List ? GetNumberOfListEntries(List) : 0;
    if (Total == 0)
        return 0;

    Items = HeapAlloc(ProcessHeap, HEAP_ZERO_MEMORY, Total * sizeof(*Items));
    if (!Items)
        return 0;

    for (Entry = GetFirstListEntry(List); Entry && Count < Total; Entry = GetNextListEntry(Entry))
    {
        PGENENTRY Data = (PGENENTRY)GetListEntryData(Entry);

        if (!Data || !Data->Id)
            continue;
        for (Index = 0; Index < Count; Index++)
        {
            if (_wcsicmp(Items[Index].Id, Data->Id) == 0)
                break;
        }
        if (Index < Count)
            continue;

        StringCchCopyW(Items[Count].Id, ARRAYSIZE(Items[Count].Id), Data->Id);
        if (!LocaleNames ||
            !GetLocaleInfoW(wcstoul(Data->Id, NULL, 16), LOCALE_SLOCALIZEDDISPLAYNAME,
                            Items[Count].Name, ARRAYSIZE(Items[Count].Name)))
        {
            StringCchCopyW(Items[Count].Name, ARRAYSIZE(Items[Count].Name), Data->Value ? Data->Value : Data->Id);
        }
        Count++;
    }

    *Choices = Items;
    return Count;
}

VOID
LoadLanguageChoices(
    _Inout_ PSETUPDATA pSetupData)
{
    if (pSetupData->Locales)
        HeapFree(ProcessHeap, 0, pSetupData->Locales);
    if (pSetupData->Layouts)
        HeapFree(ProcessHeap, 0, pSetupData->Layouts);

    pSetupData->LocaleCount = CreateChoicesFromList(pSetupData->USetupData.LanguageList, TRUE,
                                                    &pSetupData->Locales);
    pSetupData->LayoutCount = CreateChoicesFromList(pSetupData->USetupData.LayoutList, FALSE,
                                                    &pSetupData->Layouts);
}

VOID
ApplyLanguageChoice(
    _Inout_ PSETUPDATA pSetupData,
    _In_ PCWSTR LocaleId,
    _In_ PCWSTR LayoutId)
{
    WCHAR Locale[ARRAYSIZE(pSetupData->DefaultLanguage)];
    WCHAR Layout[ARRAYSIZE(pSetupData->DefaultKBLayout)];

    StringCchCopyW(Locale, ARRAYSIZE(Locale), LocaleId);
    StringCchCopyW(Layout, ARRAYSIZE(Layout), LayoutId);

    if (SelectListEntryById(pSetupData->USetupData.LanguageList, Locale))
    {
        StringCchCopyW(pSetupData->DefaultLanguage, ARRAYSIZE(pSetupData->DefaultLanguage), Locale);
        StringCchCopyW(pSetupData->USetupData.LocaleID, ARRAYSIZE(pSetupData->USetupData.LocaleID), Locale);
        pSetupData->SelectedLanguageId = pSetupData->DefaultLanguage;
        pSetupData->USetupData.LanguageId = (LANGID)(wcstoul(Locale, NULL, 16) & 0xFFFF);
    }

    if (SelectListEntryById(pSetupData->USetupData.LayoutList, Layout))
        StringCchCopyW(pSetupData->DefaultKBLayout, ARRAYSIZE(pSetupData->DefaultKBLayout), Layout);

    DPRINT1("Setup language %S, keyboard %S\n", pSetupData->DefaultLanguage, pSetupData->DefaultKBLayout);
}

static DWORD
CALLBACK
HotkeyThread(
    LPVOID Parameter)
{
    ATOM Hotkey = GlobalAddAtomW(L"Setup Shift+F10 Hotkey");
    MSG msg;

    if (!RegisterHotKey(NULL, Hotkey, MOD_SHIFT, VK_F10))
        DPRINT1("RegisterHotKey failed with %lu\n", GetLastError());

    while (GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.hwnd == NULL && msg.message == WM_HOTKEY && msg.wParam == Hotkey)
        {
            WCHAR CmdLine[] = L"cmd.exe";
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi;

            if (CreateProcessW(NULL, CmdLine, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
            {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }
    }

    UnregisterHotKey(NULL, Hotkey);
    GlobalDeleteAtom(Hotkey);
    return 0;
}

static FARPROC
WINAPI
SetupDelayHook(
    unsigned dliNotify,
    PDelayLoadInfo pdli)
{
    static CHAR DllPath[MAX_PATH];
    WCHAR ModulePath[MAX_PATH];
    PWCHAR Name;
    DWORD Length;

    if (dliNotify == dliNotePreLoadLibrary && _stricmp(pdli->szDll, "setuplib.dll") == 0)
    {
        Length = GetModuleFileNameW(NULL, ModulePath, ARRAYSIZE(ModulePath));
        if (Length && Length < ARRAYSIZE(ModulePath) && (Name = wcsrchr(ModulePath, L'\\')) != NULL)
        {
            *Name = UNICODE_NULL;
            if (SUCCEEDED(StringCchPrintfA(DllPath, ARRAYSIZE(DllPath), "%S\\system32\\%s", ModulePath, pdli->szDll)))
                pdli->szDll = DllPath;
        }
    }
    else if (dliNotify == dliFailLoadLib)
    {
        WCHAR Text[MAX_PATH + 128];

        LoadPrintfW(Text, ARRAYSIZE(Text), IDS_SETUPLIB_MISSING, pdli->szDll);
        SetupMessage(NULL, TD_ERROR_ICON, TDCBF_CLOSE_BUTTON, IDCLOSE, NULL, Text);
        ExitProcess(ERROR_MOD_NOT_FOUND);
    }

    return NULL;
}

PfnDliHook __pfnDliNotifyHook2 = SetupDelayHook;
PfnDliHook __pfnDliFailureHook2 = SetupDelayHook;

static BOOL
IsSetupAlreadyRunning(
    _In_ HINSTANCE hInstance)
{
    WCHAR Caption[128];
    HANDLE hMutex;
    HWND hWnd;

    hMutex = CreateMutexW(NULL, FALSE, L"Global\\__LiberNT_Setup__");
    if (!hMutex && GetLastError() == ERROR_PATH_NOT_FOUND)
        hMutex = CreateMutexW(NULL, FALSE, L"__LiberNT_Setup__");
    if (!hMutex || GetLastError() != ERROR_ALREADY_EXISTS)
        return FALSE;

    if (LoadStringW(hInstance, IDS_CAPTION, Caption, ARRAYSIZE(Caption)) &&
        (hWnd = FindWindowW(L"#32770", Caption)) != NULL)
    {
        hWnd = GetLastActivePopup(hWnd);
        ShowWindow(hWnd, SW_SHOWNA);
        SwitchToThisWindow(hWnd, TRUE);
    }

    CloseHandle(hMutex);
    return TRUE;
}

static VOID
RestartComputer(VOID)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE hToken;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
    {
        Privileges.PrivilegeCount = 1;
        Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &Privileges.Privileges[0].Luid))
            AdjustTokenPrivileges(hToken, FALSE, &Privileges, 0, NULL, NULL);
        CloseHandle(hToken);
    }

    ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_OPERATINGSYSTEM |
                              SHTDN_REASON_MINOR_INSTALLATION |
                              SHTDN_REASON_FLAG_PLANNED);
}

int WINAPI
wWinMain(
    HINSTANCE hInst,
    HINSTANCE hPrevInstance,
    LPWSTR lpszCmdLine,
    int nCmdShow)
{
    INITCOMMONCONTROLSEX iccx;
    HANDLE hHotkeyThread;
    HRESULT hrCom;
    ULONG Error;

    if (IsSetupAlreadyRunning(hInst))
        return 0;

    ProcessHeap = GetProcessHeap();
    SetupData.hInstance = hInst;

    iccx.dwSize = sizeof(iccx);
    iccx.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_UPDOWN_CLASS;
    InitCommonControlsEx(&iccx);

    Error = InitializeSetup(&SetupData.USetupData, NULL, &SpFileExports, &SpInfExports);
    if (Error != ERROR_SUCCESS)
    {
        SetupMessage(NULL, TD_ERROR_ICON, TDCBF_CLOSE_BUTTON, IDCLOSE,
                     MAKEINTRESOURCEW(IDS_INSTALL_FAILED), MAKEINTRESOURCEW(IDS_NO_TXTSETUP_SIF));
        goto Quit;
    }

    SetupData.bUnattend = CheckUnattendedSetup(&SetupData.USetupData);
    if (!LoadSetupData(&SetupData))
        goto Quit;

    if (wcsstr(lpszCmdLine, L"--selftest") || SetupData.USetupData.SelfTest)
    {
        Error = RunSetupSelfTest(&SetupData);
        FinishSetup(&SetupData.USetupData);
        return (int)Error;
    }

    hHotkeyThread = CreateThread(NULL, 0, HotkeyThread, NULL, 0, NULL);
    hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    RunSetupWizard(&SetupData);

    if (SUCCEEDED(hrCom))
        CoUninitialize();
    if (hHotkeyThread)
    {
        PostThreadMessageW(GetThreadId(hHotkeyThread), WM_QUIT, 0, 0);
        CloseHandle(hHotkeyThread);
    }

Quit:
    FinishSetup(&SetupData.USetupData);
    if (SetupData.bMustReboot)
        RestartComputer();
    return 0;
}
