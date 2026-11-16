/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     TPM Base Services declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _TBS_H_
#define _TBS_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef BOOL   TBS_BOOL;
typedef UINT32 TBS_RESULT;
typedef PVOID  TBS_HCONTEXT, *PTBS_HCONTEXT;
typedef UINT32 TBS_COMMAND_PRIORITY;
typedef UINT32 TBS_COMMAND_LOCALITY;
typedef UINT32 TBS_OWNERAUTH_TYPE;
typedef UINT32 TBS_HANDLE;

#define TBS_CONTEXT_VERSION_ONE         1
#define TBS_CONTEXT_VERSION_TWO         2

#define TBS_COMMAND_PRIORITY_LOW        100
#define TBS_COMMAND_PRIORITY_NORMAL     200
#define TBS_COMMAND_PRIORITY_HIGH       300
#define TBS_COMMAND_PRIORITY_SYSTEM     400
#define TBS_COMMAND_PRIORITY_MAX        0x80000000

#define TBS_COMMAND_LOCALITY_ZERO       0
#define TBS_COMMAND_LOCALITY_ONE        1
#define TBS_COMMAND_LOCALITY_TWO        2
#define TBS_COMMAND_LOCALITY_THREE      3
#define TBS_COMMAND_LOCALITY_FOUR       4

#define TBS_SUCCESS                     0
#define TBS_IN_OUT_BUF_SIZE_MAX         (256 * 1024)

#define TBS_OWNERAUTH_TYPE_FULL             1
#define TBS_OWNERAUTH_TYPE_ADMIN            2
#define TBS_OWNERAUTH_TYPE_USER             3
#define TBS_OWNERAUTH_TYPE_ENDORSEMENT      4
#define TBS_OWNERAUTH_TYPE_ENDORSEMENT_20   12
#define TBS_OWNERAUTH_TYPE_STORAGE_20       13

#define TPM_VERSION_UNKNOWN             0
#define TPM_VERSION_12                  1
#define TPM_VERSION_20                  2

#define TPM_IFTYPE_UNKNOWN              0
#define TPM_IFTYPE_1                    1
#define TPM_IFTYPE_TRUSTZONE            2
#define TPM_IFTYPE_HW                   3
#define TPM_IFTYPE_EMULATOR             4
#define TPM_IFTYPE_SPB                  5

#define TBS_TCGLOG_SRTM_CURRENT         0
#define TBS_TCGLOG_DRTM_CURRENT         1
#define TBS_TCGLOG_SRTM_BOOT            2
#define TBS_TCGLOG_SRTM_RESUME          3
#define TBS_TCGLOG_DRTM_BOOT            4
#define TBS_TCGLOG_DRTM_RESUME          5

typedef struct tdTBS_CONTEXT_PARAMS
{
    UINT32 version;
} TBS_CONTEXT_PARAMS, *PTBS_CONTEXT_PARAMS;
typedef const TBS_CONTEXT_PARAMS *PCTBS_CONTEXT_PARAMS;

typedef struct tdTBS_CONTEXT_PARAMS2
{
    UINT32 version;
    union
    {
        struct
        {
            UINT32 requestRaw : 1;
            UINT32 includeTpm12 : 1;
            UINT32 includeTpm20 : 1;
        };
        UINT32 asUINT32;
    };
} TBS_CONTEXT_PARAMS2, *PTBS_CONTEXT_PARAMS2;
typedef const TBS_CONTEXT_PARAMS2 *PCTBS_CONTEXT_PARAMS2;

typedef struct _TPM_DEVICE_INFO
{
    UINT32 structVersion;
    UINT32 tpmVersion;
    UINT32 tpmInterfaceType;
    UINT32 tpmImpRevision;
} TPM_DEVICE_INFO, *PTPM_DEVICE_INFO;
typedef const TPM_DEVICE_INFO *PCTPM_DEVICE_INFO;

TBS_RESULT WINAPI Tbsi_Context_Create(_In_ PCTBS_CONTEXT_PARAMS pContextParams, _Out_ PTBS_HCONTEXT phContext);
TBS_RESULT WINAPI Tbsip_Context_Close(_In_ TBS_HCONTEXT hContext);
TBS_RESULT WINAPI Tbsip_Submit_Command(_In_ TBS_HCONTEXT hContext, _In_ TBS_COMMAND_LOCALITY Locality, _In_ TBS_COMMAND_PRIORITY Priority, _In_reads_bytes_(cbCommand) const BYTE *pabCommand, _In_ UINT32 cbCommand, _Out_writes_bytes_(*pcbResult) PBYTE pabResult, _Inout_ PUINT32 pcbResult);
TBS_RESULT WINAPI Tbsip_Cancel_Commands(_In_ TBS_HCONTEXT hContext);
TBS_RESULT WINAPI Tbsi_Physical_Presence_Command(_In_ TBS_HCONTEXT hContext, _In_reads_bytes_(cbInput) const BYTE *pabInput, _In_ UINT32 cbInput, _Out_writes_bytes_(*pcbOutput) PBYTE pabOutput, _Out_ PUINT32 pcbOutput);
TBS_RESULT WINAPI Tbsi_Get_TCG_Log(_In_ TBS_HCONTEXT hContext, _Out_writes_bytes_to_opt_(*pOutputBufLen, *pOutputBufLen) PBYTE pOutputBuf, _Inout_ PUINT32 pOutputBufLen);
TBS_RESULT WINAPI Tbsi_Get_TCG_Log_Ex(_In_ UINT32 logType, _Out_writes_bytes_opt_(*pcbOutput) PBYTE pbOutput, _Inout_ PUINT32 pcbOutput);
TBS_RESULT WINAPI Tbsi_GetDeviceInfo(_In_ UINT32 Size, _Out_writes_bytes_(Size) PVOID Info);
TBS_RESULT WINAPI Tbsi_Get_OwnerAuth(_In_ TBS_HCONTEXT hContext, _In_ TBS_OWNERAUTH_TYPE ownerauthType, _Out_writes_bytes_to_opt_(*pOutputBufLen, *pOutputBufLen) PBYTE pOutputBuf, _Inout_ PUINT32 pOutputBufLen);
TBS_RESULT WINAPI Tbsi_Revoke_Attestation(void);
HRESULT WINAPI GetDeviceID(_Out_writes_bytes_to_opt_(cbWindowsAIK, *pcbResult) PBYTE pbWindowsAIK, _In_ UINT32 cbWindowsAIK, _Out_ PUINT32 pcbResult, _Out_opt_ BOOL *pfProtectedByTPM);
HRESULT WINAPI GetDeviceIDString(_Out_writes_to_opt_(cchWindowsAIK, *pcchResult) PWSTR pszWindowsAIK, _In_ UINT32 cchWindowsAIK, _Out_ PUINT32 pcchResult, _Out_opt_ BOOL *pfProtectedByTPM);

#ifdef __cplusplus
}
#endif

#endif /* _TBS_H_ */
