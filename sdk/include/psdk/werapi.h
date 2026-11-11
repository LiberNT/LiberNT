/*
 * Windows Error Reporing definitions
 *
 * Copyright (C) 2010 Louis Lenders
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_WERAPI_H
#define __WINE_WERAPI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Only 10 parameter are allowed in WerReportSetParameter */
#define WER_MAX_PARAM_COUNT 10
#define WER_P0 0
#define WER_P1 1
#define WER_P2 2
#define WER_P3 3
#define WER_P4 4
#define WER_P5 5
#define WER_P6 6
#define WER_P7 7
#define WER_P8 8
#define WER_P9 9

/* Flags for WerReportSubmit */
#define WER_SUBMIT_HONOR_RECOVERY           0x0001
#define WER_SUBMIT_HONOR_RESTART            0x0002
#define WER_SUBMIT_QUEUE                    0x0004
#define WER_SUBMIT_SHOW_DEBUG               0x0008
#define WER_SUBMIT_ADD_REGISTERED_DATA      0x0010
#define WER_SUBMIT_OUTOFPROCESS             0x0020
#define WER_SUBMIT_NO_CLOSE_UI              0x0040
#define WER_SUBMIT_NO_QUEUE                 0x0080
#define WER_SUBMIT_NO_ARCHIVE               0x0100
#define WER_SUBMIT_START_MINIMIZED          0x0200
#define WER_SUBMIT_OUTOFPROCESS_ASYNC       0x0400
#define WER_SUBMIT_BYPASS_DATA_THROTTLING   0x0800
#define WER_SUBMIT_ARCHIVE_PARAMETERS_ONLY  0x1000
#define WER_SUBMIT_REPORT_MACHINE_ID        0x2000
#define WER_SUBMIT_BYPASS_POWER_THROTTLING  0x4000
#define WER_SUBMIT_BYPASS_NETWORK_COST_THROTTLING 0x8000
#define WER_SUBMIT_DISCARD_IF_QUEUED        WER_SUBMIT_NO_QUEUE

#define WER_FAULT_REPORTING_FLAG_NOHEAP                     1
#define WER_FAULT_REPORTING_FLAG_QUEUE                      2
#define WER_FAULT_REPORTING_FLAG_DISABLE_THREAD_SUSPENSION  4
#define WER_FAULT_REPORTING_FLAG_QUEUE_UPLOAD               8
#define WER_FAULT_REPORTING_ALWAYS_SHOW_UI                  16
#define WER_FAULT_REPORTING_NO_UI                           32
#define WER_FAULT_REPORTING_FLAG_NO_HEAP_ON_QUEUE           64
#define WER_FAULT_REPORTING_DISABLE_SNAPSHOT_CRASH          128
#define WER_FAULT_REPORTING_DISABLE_SNAPSHOT_HANG           256
#define WER_FAULT_REPORTING_CRITICAL                        512
#define WER_FAULT_REPORTING_DURABLE                         1024

#define WER_MAX_MEM_BLOCK_SIZE (64 * 1024)

#define WER_E_INSUFFICIENT_BUFFER   (HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
#define WER_E_NOT_FOUND             (HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
#define WER_E_LENGTH_EXCEEDED       (HRESULT_FROM_WIN32(ERROR_PARAMETER_QUOTA_EXCEEDED))
#define WER_E_INVALID_STATE         (HRESULT_FROM_WIN32(ERROR_INVALID_STATE))
#define WER_E_MISSING_DUMP          (HRESULT_FROM_WIN32(ERROR_MISSING_SYSTEMFILE))
#define WER_E_CABBING_FAILURE       (HRESULT_FROM_WIN32(ERROR_GEN_FAILURE))

#define WER_FILE_DELETE_WHEN_DONE   1
#define WER_FILE_ANONYMOUS_DATA     2
#define WER_FILE_COMPRESSED         4

#define WER_MAX_REGISTERED_ENTRIES                      512
#define WER_MAX_REGISTERED_METADATA                     8
#define WER_MAX_REGISTERED_DUMPCOLLECTION               4
#define WER_METADATA_KEY_MAX_LENGTH                     64
#define WER_METADATA_VALUE_MAX_LENGTH                   128
#define WER_MAX_SIGNATURE_NAME_LENGTH                   128
#define WER_MAX_EVENT_NAME_LENGTH                       64
#define WER_MAX_PARAM_LENGTH                            (MAX_PATH)
#define WER_MAX_LOCAL_DUMP_SUBPATH_LENGTH               64
#define WER_MAX_REGISTERED_RUNTIME_EXCEPTION_MODULES    16

#define WER_MAX_TOTAL_PARAM_LENGTH          1720
#define WER_MAX_PREFERRED_MODULES           128
#define WER_MAX_PREFERRED_MODULES_BUFFER    256

#define APPCRASH_EVENT          L"APPCRASH"
#define PACKAGED_APPCRASH_EVENT L"MoAppCrash"

#define WER_DUMP_MASK_START                  1
#define WER_DUMP_MASK_DUMPTYPE               (WER_DUMP_MASK_START << 0)
#define WER_DUMP_MASK_ONLY_THISTHREAD        (WER_DUMP_MASK_START << 1)
#define WER_DUMP_MASK_THREADFLAGS            (WER_DUMP_MASK_START << 2)
#define WER_DUMP_MASK_THREADFLAGS_EX         (WER_DUMP_MASK_START << 3)
#define WER_DUMP_MASK_OTHERTHREADFLAGS       (WER_DUMP_MASK_START << 4)
#define WER_DUMP_MASK_OTHERTHREADFLAGS_EX    (WER_DUMP_MASK_START << 5)
#define WER_DUMP_MASK_PREFERRED_MODULESFLAGS (WER_DUMP_MASK_START << 6)
#define WER_DUMP_MASK_OTHER_MODULESFLAGS     (WER_DUMP_MASK_START << 7)
#define WER_DUMP_MASK_PREFERRED_MODULE_LIST  (WER_DUMP_MASK_START << 8)

#define WER_DUMP_NOHEAP_ONQUEUE 1
#define WER_DUMP_AUXILIARY      2

#define WER_RUNTIME_EXCEPTION_EVENT_FUNCTION            "OutOfProcessExceptionEventCallback"
#define WER_RUNTIME_EXCEPTION_EVENT_SIGNATURE_FUNCTION  "OutOfProcessExceptionEventSignatureCallback"
#define WER_RUNTIME_EXCEPTION_DEBUGGER_LAUNCH           "OutOfProcessExceptionEventDebuggerLaunchCallback"

/* #### */

typedef HANDLE HREPORT;

typedef enum _WER_CONSENT
{
    WerConsentNotAsked = 1,
    WerConsentApproved,
    WerConsentDenied,
    WerConsentAlwaysPrompt,
    WerConsentMax
} WER_CONSENT;

typedef enum _WER_REGISTER_FILE_TYPE
{
    WerRegFileTypeUserDocument = 1,
    WerRegFileTypeOther = 2,
    WerRegFileTypeMax
} WER_REGISTER_FILE_TYPE;

typedef struct _WER_REPORT_INFORMATION
{
    DWORD   dwSize;
    HANDLE  hProcess;
    WCHAR   wzConsentKey[64];
    WCHAR   wzFriendlyEventName[128];
    WCHAR   wzApplicationName[128];
    WCHAR   wzApplicationPath[MAX_PATH];
    WCHAR   wzDescription[512];
    HWND    hwndParent;
} WER_REPORT_INFORMATION, *PWER_REPORT_INFORMATION;


typedef enum _WER_REPORT_TYPE
{
    WerReportNonCritical = 0,
    WerReportCritical,
    WerReportApplicationCrash,
    WerReportApplicationHang,
    WerReportKernel,
    WerReportInvalid
} WER_REPORT_TYPE;

typedef enum _WER_SUBMIT_RESULT
{
    WerReportQueued = 1,
    WerReportUploaded,
    WerReportDebug,
    WerReportFailed,
    WerDisabled,
    WerReportCancelled,
    WerDisabledQueue,
    WerReportAsync,
    WerCustomAction,
    WerThrottled,
    WerReportUploadedCab,
    WerStorageLocationNotFound,
    WerSubmitResultMax
} WER_SUBMIT_RESULT, *PWER_SUBMIT_RESULT;

typedef enum _WER_REPORT_UI
{
    WerUIAdditionalDataDlgHeader = 1,
    WerUIIconFilePath = 2,
    WerUIConsentDlgHeader = 3,
    WerUIConsentDlgBody = 4,
    WerUIOnlineSolutionCheckText = 5,
    WerUIOfflineSolutionCheckText = 6,
    WerUICloseText = 7,
    WerUICloseDlgHeader = 8,
    WerUICloseDlgBody = 9,
    WerUICloseDlgButtonText = 10,
    WerUIMax
} WER_REPORT_UI;

typedef enum _WER_FILE_TYPE
{
    WerFileTypeMicrodump = 1,
    WerFileTypeMinidump = 2,
    WerFileTypeHeapdump = 3,
    WerFileTypeUserDocument = 4,
    WerFileTypeOther = 5,
    WerFileTypeTriagedump = 6,
    WerFileTypeCustomDump = 7,
    WerFileTypeAuxiliaryDump = 8,
    WerFileTypeEtlTrace = 9,
    WerFileTypeMax
} WER_FILE_TYPE;

typedef enum _WER_DUMP_TYPE
{
    WerDumpTypeMicroDump = 1,
    WerDumpTypeMiniDump = 2,
    WerDumpTypeHeapDump = 3,
    WerDumpTypeTriageDump = 4,
    WerDumpTypeMax = 5
} WER_DUMP_TYPE;

typedef struct _WER_DUMP_CUSTOM_OPTIONS
{
    DWORD dwSize;
    DWORD dwMask;
    DWORD dwDumpFlags;
    BOOL  bOnlyThisThread;
    DWORD dwExceptionThreadFlags;
    DWORD dwOtherThreadFlags;
    DWORD dwExceptionThreadExFlags;
    DWORD dwOtherThreadExFlags;
    DWORD dwPreferredModuleFlags;
    DWORD dwOtherModuleFlags;
    WCHAR wzPreferredModuleList[WER_MAX_PREFERRED_MODULES_BUFFER];
} WER_DUMP_CUSTOM_OPTIONS, *PWER_DUMP_CUSTOM_OPTIONS;

typedef struct _WER_EXCEPTION_INFORMATION
{
    PEXCEPTION_POINTERS pExceptionPointers;
    BOOL bClientPointers;
} WER_EXCEPTION_INFORMATION, *PWER_EXCEPTION_INFORMATION;

typedef struct _WER_RUNTIME_EXCEPTION_INFORMATION
{
    DWORD dwSize;
    HANDLE hProcess;
    HANDLE hThread;
    EXCEPTION_RECORD exceptionRecord;
    CONTEXT context;
    PCWSTR pwszReportId;
} WER_RUNTIME_EXCEPTION_INFORMATION, *PWER_RUNTIME_EXCEPTION_INFORMATION;

typedef HRESULT (*PFN_WER_RUNTIME_EXCEPTION_EVENT)(PVOID pContext,
                                                   const PWER_RUNTIME_EXCEPTION_INFORMATION pExceptionInformation,
                                                   BOOL *pbOwnershipClaimed, PWSTR pwszEventName,
                                                   PDWORD pchSize, PDWORD pdwSignatureCount);
typedef HRESULT (*PFN_WER_RUNTIME_EXCEPTION_EVENT_SIGNATURE)(PVOID pContext,
                                                             const PWER_RUNTIME_EXCEPTION_INFORMATION pExceptionInformation,
                                                             DWORD dwIndex, PWSTR pwszName, PDWORD pchName,
                                                             PWSTR pwszValue, PDWORD pchValue);
typedef HRESULT (*PFN_WER_RUNTIME_EXCEPTION_DEBUGGER_LAUNCH)(PVOID pContext,
                                                             const PWER_RUNTIME_EXCEPTION_INFORMATION pExceptionInformation,
                                                             PBOOL pbIsCustomDebugger, PWSTR pwszDebuggerLaunch,
                                                             PDWORD pchDebuggerLaunch, PBOOL pbIsDebuggerAutolaunch);

typedef enum _REPORT_STORE_TYPES
{
    E_STORE_USER_ARCHIVE = 0,
    E_STORE_USER_QUEUE = 1,
    E_STORE_MACHINE_ARCHIVE = 2,
    E_STORE_MACHINE_QUEUE = 3,
    E_STORE_INVALID = 4
} REPORT_STORE_TYPES;

typedef PVOID HREPORTSTORE, *PHREPORTSTORE;

typedef struct _WER_REPORT_PARAMETER
{
    WCHAR Name[WER_MAX_SIGNATURE_NAME_LENGTH + 1];
    WCHAR Value[WER_MAX_PARAM_LENGTH];
} WER_REPORT_PARAMETER, PWER_REPORT_PARAMETER;

typedef struct _WER_REPORT_SIGNATURE
{
    WCHAR EventName[WER_MAX_EVENT_NAME_LENGTH + 1];
    WER_REPORT_PARAMETER Parameters[WER_MAX_PARAM_COUNT];
} WER_REPORT_SIGNATURE, *PWER_REPORT_SIGNATURE;

typedef struct _WER_REPORT_METADATA_V1
{
    WER_REPORT_SIGNATURE Signature;
    GUID BucketId;
    GUID ReportId;
    FILETIME CreationTime;
    ULONGLONG SizeInBytes;
} WER_REPORT_METADATA_V1, *PWER_REPORT_METADATA_V1;

typedef struct _WER_REPORT_METADATA_V2
{
    WER_REPORT_SIGNATURE Signature;
    GUID BucketId;
    GUID ReportId;
    FILETIME CreationTime;
    ULONGLONG SizeInBytes;
    WCHAR CabId[MAX_PATH];
    DWORD ReportStatus;
    GUID ReportIntegratorId;
    DWORD NumberOfFiles;
    DWORD SizeOfFileNames;
    WCHAR *FileNames;
} WER_REPORT_METADATA_V2, *PWER_REPORT_METADATA_V2;

/* #### */

HRESULT WINAPI WerAddExcludedApplication(PCWSTR, BOOL);
VOID WerFreeString(PCWSTR);
HRESULT WINAPI WerGetFlags(HANDLE process, DWORD *flags);
HRESULT WINAPI WerRegisterFile(PCWSTR file, WER_REGISTER_FILE_TYPE regfiletype, DWORD flags);
HRESULT WINAPI WerRegisterMemoryBlock(void *block, DWORD size);
HRESULT WINAPI WerRegisterRuntimeExceptionModule(PCWSTR callbackdll, void *context);
HRESULT WINAPI WerRemoveExcludedApplication(PCWSTR, BOOL);
HRESULT WINAPI WerReportAddDump(HREPORT, HANDLE, HANDLE, WER_DUMP_TYPE, PWER_EXCEPTION_INFORMATION,
                                PWER_DUMP_CUSTOM_OPTIONS, DWORD);
HRESULT WINAPI WerReportAddFile(HREPORT, PCWSTR, WER_FILE_TYPE, DWORD);
HRESULT WINAPI WerReportCloseHandle(HREPORT);
HRESULT WINAPI WerReportCreate(PCWSTR, WER_REPORT_TYPE, PWER_REPORT_INFORMATION, HREPORT*);
HRESULT WINAPI WerReportSetParameter(HREPORT, DWORD, PCWSTR, PCWSTR);
HRESULT WINAPI WerReportSetUIOption(HREPORT, WER_REPORT_UI, PCWSTR);
HRESULT WINAPI WerReportSubmit(HREPORT, WER_CONSENT, DWORD, PWER_SUBMIT_RESULT);
HRESULT WINAPI WerSetFlags(DWORD flags);
VOID WerStoreClose(HREPORTSTORE);
HRESULT WerStoreGetFirstReportKey(HREPORTSTORE, PCWSTR *);
HRESULT WerStoreGetNextReportKey(HREPORTSTORE, PCWSTR *);
HRESULT WerStoreGetReportCount(HREPORTSTORE, DWORD *);
HRESULT WerStoreGetSizeOnDisk(HREPORTSTORE, ULONGLONG *);
HRESULT WerStoreOpen(REPORT_STORE_TYPES, PHREPORTSTORE);
HRESULT WerStoreQueryReportMetadataV1(HREPORTSTORE, PCWSTR, PWER_REPORT_METADATA_V1);
HRESULT WerStoreQueryReportMetadataV2(HREPORTSTORE, PCWSTR, PWER_REPORT_METADATA_V2);
HRESULT WINAPI WerUnregisterFile(PCWSTR file);
HRESULT WINAPI WerUnregisterMemoryBlock(void *block);
HRESULT WINAPI WerUnregisterRuntimeExceptionModule(PCWSTR callbackdll, void *context);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_WERAPI_H */
