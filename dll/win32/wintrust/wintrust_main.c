/*
 * Copyright 2001 Rein Klazes
 * Copyright 2007 Juan Lang
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

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winreg.h"
#include "guiddef.h"
#include "wintrust.h"
#include "softpub.h"
#include "mscat.h"
#include "objbase.h"
#include "winuser.h"
#include "cryptdlg.h"
#include "cryptuiapi.h"
#include "wintrust_priv.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wintrust);

/***********************************************************************
 *		TrustIsCertificateSelfSigned (WINTRUST.@)
 */
BOOL WINAPI TrustIsCertificateSelfSigned( PCCERT_CONTEXT cert )
{
    PCERT_EXTENSION ext;
    DWORD size;
    BOOL ret;

    TRACE("%p\n", cert);
    if ((ext = CertFindExtension(szOID_AUTHORITY_KEY_IDENTIFIER2,
     cert->pCertInfo->cExtension, cert->pCertInfo->rgExtension)))
    {
        CERT_AUTHORITY_KEY_ID2_INFO *info;

        ret = CryptDecodeObjectEx(cert->dwCertEncodingType,
         X509_AUTHORITY_KEY_ID2, ext->Value.pbData, ext->Value.cbData,
         CRYPT_DECODE_ALLOC_FLAG | CRYPT_DECODE_NOCOPY_FLAG, NULL,
         &info, &size);
        if (ret)
        {
            if (info->AuthorityCertIssuer.cAltEntry &&
             info->AuthorityCertSerialNumber.cbData)
            {
                PCERT_ALT_NAME_ENTRY directoryName = NULL;
                DWORD i;

                for (i = 0; !directoryName &&
                 i < info->AuthorityCertIssuer.cAltEntry; i++)
                    if (info->AuthorityCertIssuer.rgAltEntry[i].dwAltNameChoice
                     == CERT_ALT_NAME_DIRECTORY_NAME)
                        directoryName =
                         &info->AuthorityCertIssuer.rgAltEntry[i];
                if (directoryName)
                {
                    ret = CertCompareCertificateName(cert->dwCertEncodingType,
                     &directoryName->DirectoryName, &cert->pCertInfo->Issuer)
                     && CertCompareIntegerBlob(&info->AuthorityCertSerialNumber,
                     &cert->pCertInfo->SerialNumber);
                }
                else
                {
                    FIXME("no supported name type in authority key id2\n");
                    ret = FALSE;
                }
            }
            else if (info->KeyId.cbData)
            {
                ret = CertGetCertificateContextProperty(cert,
                 CERT_KEY_IDENTIFIER_PROP_ID, NULL, &size);
                if (ret && size == info->KeyId.cbData)
                {
                    LPBYTE buf = CryptMemAlloc(size);

                    if (buf)
                    {
                        CertGetCertificateContextProperty(cert,
                         CERT_KEY_IDENTIFIER_PROP_ID, buf, &size);
                        ret = !memcmp(buf, info->KeyId.pbData, size);
                        CryptMemFree(buf);
                    }
                    else
                        ret = FALSE;
                }
                else
                    ret = FALSE;
            }
            LocalFree(info);
        }
    }
    else if ((ext = CertFindExtension(szOID_AUTHORITY_KEY_IDENTIFIER,
     cert->pCertInfo->cExtension, cert->pCertInfo->rgExtension)))
    {
        CERT_AUTHORITY_KEY_ID_INFO *info;

        ret = CryptDecodeObjectEx(cert->dwCertEncodingType,
         X509_AUTHORITY_KEY_ID, ext->Value.pbData, ext->Value.cbData,
         CRYPT_DECODE_ALLOC_FLAG | CRYPT_DECODE_NOCOPY_FLAG, NULL,
         &info, &size);
        if (ret)
        {
            if (info->CertIssuer.cbData && info->CertSerialNumber.cbData)
            {
                ret = CertCompareCertificateName(cert->dwCertEncodingType,
                 &info->CertIssuer, &cert->pCertInfo->Issuer) &&
                 CertCompareIntegerBlob(&info->CertSerialNumber,
                 &cert->pCertInfo->SerialNumber);
            }
            else if (info->KeyId.cbData)
            {
                ret = CertGetCertificateContextProperty(cert,
                 CERT_KEY_IDENTIFIER_PROP_ID, NULL, &size);
                if (ret && size == info->KeyId.cbData)
                {
                    LPBYTE buf = CryptMemAlloc(size);

                    if (buf)
                    {
                        CertGetCertificateContextProperty(cert,
                         CERT_KEY_IDENTIFIER_PROP_ID, buf, &size);
                        ret = !memcmp(buf, info->KeyId.pbData, size);
                        CryptMemFree(buf);
                    }
                    else
                        ret = FALSE;
                }
                else
                    ret = FALSE;
            }
            else
                ret = FALSE;
            LocalFree(info);
        }
    }
    else
        ret = CertCompareCertificateName(cert->dwCertEncodingType,
         &cert->pCertInfo->Subject, &cert->pCertInfo->Issuer);
    return ret;
}

typedef HRESULT (WINAPI *wintrust_step_func)(CRYPT_PROVIDER_DATA *data);

struct wintrust_step
{
    wintrust_step_func func;
    DWORD              error_index;
};

#ifdef __REACTOS__
static DWORD WINTRUST_ExecuteSteps(const struct wintrust_step *steps,
 DWORD numSteps, CRYPT_PROVIDER_DATA *provData)
{
    DWORD i, err = ERROR_SUCCESS, first = ERROR_SUCCESS;
    BOOL has_final = FALSE;
    HRESULT ret;

    for (i = 0; i < numSteps; i++)
    {
        ret = steps[i].func(provData);
        if (steps[i].error_index == TRUSTERROR_STEP_FINAL_WVTINIT && ret != S_OK)
        {
            provData->psPfns->pfnCleanupPolicy = NULL;
            return ret;
        }
        if (steps[i].error_index == TRUSTERROR_STEP_FINAL_POLICYPROV)
        {
            has_final = TRUE;
            err = ret;
        }
        else if (!first && ret != S_OK)
            first = provData->padwTrustStepErrors[steps[i].error_index];
    }
    return has_final ? err : first;
}
#else
static DWORD WINTRUST_ExecuteSteps(const struct wintrust_step *steps,
 DWORD numSteps, CRYPT_PROVIDER_DATA *provData)
{
    DWORD i, err = ERROR_SUCCESS;

    for (i = 0; !err && i < numSteps; i++)
    {
        err = steps[i].func(provData);
        if (err)
            err = provData->padwTrustStepErrors[steps[i].error_index];
    }
    return err;
}
#endif

#ifdef __REACTOS__
void WINTRUST_FreeSgnr(CRYPT_PROVIDER_DATA *data, CRYPT_PROVIDER_SGNR *sgnr)
{
    DWORD i;

    for (i = 0; i < sgnr->csCounterSigners; i++)
        WINTRUST_FreeSgnr(data, &sgnr->pasCounterSigners[i]);
    data->psPfns->pfnFree(sgnr->pasCounterSigners);
    for (i = 0; i < sgnr->csCertChain; i++)
        CertFreeCertificateContext(sgnr->pasCertChain[i].pCert);
    data->psPfns->pfnFree(sgnr->pasCertChain);
    data->psPfns->pfnFree(sgnr->psSigner);
    CertFreeCertificateChain(sgnr->pChainContext);
}

static void WINTRUST_FreeProviderData(CRYPT_PROVIDER_DATA *provData)
{
    if (provData->hProv)
        CryptReleaseContext(provData->hProv, 0);
    free(provData->padwTrustStepErrors);
    free(provData->pPDSip);
    free(provData->psPfns);
    free(provData);
}

static void WINTRUST_SetUiFunctions(CRYPT_PROVIDER_DATA *provData)
{
    CRYPT_PROVUI_FUNCS *ui = (CRYPT_PROVUI_FUNCS *)(provData->psPfns + 1);
    CRYPT_PROVUI_DATA *uiData = (CRYPT_PROVUI_DATA *)(ui + 1);

    ui->cbStruct = sizeof(*ui);
    ui->psUIData = uiData;
    uiData->cbStruct = sizeof(*uiData);
    provData->psPfns->psUIpfns = ui;
}

#endif
static CRYPT_PROVIDER_DATA *WINTRUST_AllocateProviderData(void)
{
    CRYPT_PROVIDER_DATA *provData;

    provData = calloc(1, sizeof(CRYPT_PROVIDER_DATA));
    if (!provData)
        goto oom;
    provData->cbStruct = sizeof(CRYPT_PROVIDER_DATA);

    provData->padwTrustStepErrors = calloc(TRUSTERROR_MAX_STEPS, sizeof(DWORD));
    if (!provData->padwTrustStepErrors)
        goto oom;
    provData->cdwTrustStepErrors = TRUSTERROR_MAX_STEPS;

    provData->pPDSip = calloc(1, sizeof(PROVDATA_SIP));
    if (!provData->pPDSip)
        goto oom;
    provData->pPDSip->cbStruct = sizeof(PROVDATA_SIP);

#ifdef __REACTOS__
    provData->psPfns = calloc(1, sizeof(CRYPT_PROVIDER_FUNCTIONS) + sizeof(CRYPT_PROVUI_FUNCS) + sizeof(CRYPT_PROVUI_DATA));
#else
    provData->psPfns = calloc(1, sizeof(CRYPT_PROVIDER_FUNCTIONS));
#endif
    if (!provData->psPfns)
        goto oom;
    provData->psPfns->cbStruct = sizeof(CRYPT_PROVIDER_FUNCTIONS);
#ifdef __REACTOS__
    GetSystemTimeAsFileTime(&provData->sftSystemTime);
    provData->dwEncoding = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    if (!CryptAcquireContextW(&provData->hProv, NULL, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        provData->hProv = 0;
#endif
    return provData;

oom:
    if (provData)
    {
        free(provData->padwTrustStepErrors);
        free(provData->pPDSip);
        free(provData->psPfns);
        free(provData);
    }
    return NULL;
}

/* Adds trust steps for each function in psPfns.  Assumes steps has at least
 * 5 entries.  Returns the number of steps added.
 */
static DWORD WINTRUST_AddTrustStepsFromFunctions(struct wintrust_step *steps,
 const CRYPT_PROVIDER_FUNCTIONS *psPfns)
{
    DWORD numSteps = 0;

    if (psPfns->pfnInitialize)
    {
        steps[numSteps].func = psPfns->pfnInitialize;
        steps[numSteps++].error_index = TRUSTERROR_STEP_FINAL_WVTINIT;
    }
    if (psPfns->pfnObjectTrust)
    {
        steps[numSteps].func = psPfns->pfnObjectTrust;
        steps[numSteps++].error_index = TRUSTERROR_STEP_FINAL_OBJPROV;
    }
    if (psPfns->pfnSignatureTrust)
    {
        steps[numSteps].func = psPfns->pfnSignatureTrust;
        steps[numSteps++].error_index = TRUSTERROR_STEP_FINAL_SIGPROV;
    }
    if (psPfns->pfnCertificateTrust)
    {
        steps[numSteps].func = psPfns->pfnCertificateTrust;
        steps[numSteps++].error_index = TRUSTERROR_STEP_FINAL_CERTPROV;
    }
    if (psPfns->pfnFinalPolicy)
    {
        steps[numSteps].func = psPfns->pfnFinalPolicy;
        steps[numSteps++].error_index = TRUSTERROR_STEP_FINAL_POLICYPROV;
    }
    return numSteps;
}

static LONG WINTRUST_DefaultVerify(HWND hwnd, GUID *actionID,
 WINTRUST_DATA *data)
{
    DWORD err = ERROR_SUCCESS, numSteps = 0;
    CRYPT_PROVIDER_DATA *provData;
    BOOL ret;
    struct wintrust_step verifySteps[5];

    TRACE("(%p, %s, %p)\n", hwnd, debugstr_guid(actionID), data);

    provData = WINTRUST_AllocateProviderData();
    if (!provData)
        return ERROR_OUTOFMEMORY;

    ret = WintrustLoadFunctionPointers(actionID, provData->psPfns);
#ifdef __REACTOS__
    if (ret && !WINTRUST_AddTrustStepsFromFunctions(verifySteps, provData->psPfns))
    {
        SetLastError(TRUST_E_PROVIDER_UNKNOWN);
        ret = FALSE;
    }
#endif
    if (!ret)
    {
        err = GetLastError();
        goto error;
    }
#ifdef __REACTOS__
    WINTRUST_SetUiFunctions(provData);
#endif

    data->hWVTStateData = provData;
    provData->pWintrustData = data;

    if (WVT_ISINSTRUCT(WINTRUST_DATA, data->cbStruct, pSignatureSettings))
        provData->pSigSettings = data->pSignatureSettings;

    provData->hWndParent = hwnd;
    provData->pgActionID = actionID;
    WintrustGetRegPolicyFlags(&provData->dwRegPolicySettings);
#ifdef __REACTOS__
    provData->dwProvFlags = CPD_USE_NT5_CHAIN_FLAG;
    if (WVT_ISINSTRUCT(WINTRUST_DATA, data->cbStruct, dwProvFlags))
        provData->dwProvFlags |= data->dwProvFlags;
    /* TODO: INFO_NEEDED: where Windows 11 reads this value from; it reports 1 for every action measured */
    provData->dwRegSecuritySettings = 1;
#endif

    numSteps = WINTRUST_AddTrustStepsFromFunctions(verifySteps,
     provData->psPfns);
    err = WINTRUST_ExecuteSteps(verifySteps, numSteps, provData);
#ifdef __REACTOS__
    while (err && provData->pSigState && provData->pSigState->fFirstAttemptMade &&
           !provData->pSigState->fNoMoreSigs)
    {
        DWORD i, first;

        for (first = 0; first < numSteps; first++)
            if (verifySteps[first].error_index == TRUSTERROR_STEP_FINAL_SIGPROV)
                break;
        if (first == numSteps)
            break;

        for (i = 0; i < provData->csSigners; i++)
            WINTRUST_FreeSgnr(provData, &provData->pasSigners[i]);
        provData->psPfns->pfnFree(provData->pasSigners);
        provData->pasSigners = NULL;
        provData->csSigners = 0;
        for (i = first; i < numSteps; i++)
            provData->padwTrustStepErrors[verifySteps[i].error_index] = 0;

        err = WINTRUST_ExecuteSteps(&verifySteps[first], numSteps - first, provData);
    }
    if (err && WVT_ISINSTRUCT(WINTRUST_DATA, data->cbStruct, dwProvFlags) &&
        (data->dwProvFlags & WTD_HASH_ONLY_FLAG))
    {
        err = TRUST_E_NOSIGNATURE;
    }
    if (provData->pPDSip && provData->pPDSip->pSip)
    {
        provData->psPfns->pfnFree(provData->pPDSip->pSip);
        provData->pPDSip->pSip = NULL;
    }
#endif
    goto done;

error:
#ifdef __REACTOS__
    WINTRUST_FreeProviderData(provData);
#else
    free(provData->padwTrustStepErrors);
    free(provData->pPDSip);
    free(provData->psPfns);
    free(provData);
#endif

done:
    TRACE("returning %08lx\n", err);
    return err;
}

static LONG WINTRUST_DefaultClose(HWND hwnd, GUID *actionID,
 WINTRUST_DATA *data)
{
    DWORD err = ERROR_SUCCESS;
    CRYPT_PROVIDER_DATA *provData = data->hWVTStateData;

    TRACE("(%p, %s, %p)\n", hwnd, debugstr_guid(actionID), data);

    if (provData)
    {
        if (provData->psPfns->pfnCleanupPolicy)
            err = provData->psPfns->pfnCleanupPolicy(provData);

#ifdef __REACTOS__
        WINTRUST_FreeProviderData(provData);
#else
        free(provData->padwTrustStepErrors);
        free(provData->pPDSip);
        free(provData->psPfns);
        free(provData);
#endif
        data->hWVTStateData = NULL;
    }
    TRACE("returning %08lx\n", err);
    return err;
}

static LONG WINTRUST_DefaultVerifyAndClose(HWND hwnd, GUID *actionID,
 WINTRUST_DATA *data)
{
    LONG err;

    TRACE("(%p, %s, %p)\n", hwnd, debugstr_guid(actionID), data);

    err = WINTRUST_DefaultVerify(hwnd, actionID, data);
    WINTRUST_DefaultClose(hwnd, actionID, data);
    TRACE("returning %08lx\n", err);
    return err;
}

static LONG WINTRUST_PublishedSoftware(HWND hwnd, GUID *actionID,
 WINTRUST_DATA *data)
{
    WINTRUST_DATA wintrust_data = { sizeof(wintrust_data), 0 };
    /* Undocumented: the published software action is passed a path,
     * and pSIPClientData points to a WIN_TRUST_SUBJECT_FILE.
     */
    LPWIN_TRUST_SUBJECT_FILE subjectFile = data->pSIPClientData;
    WINTRUST_FILE_INFO fileInfo = { sizeof(fileInfo), 0 };

    TRACE("subjectFile->hFile: %p\n", subjectFile->hFile);
    TRACE("subjectFile->lpPath: %s\n", debugstr_w(subjectFile->lpPath));
    fileInfo.pcwszFilePath = subjectFile->lpPath;
    fileInfo.hFile = subjectFile->hFile;
    wintrust_data.pFile = &fileInfo;
    wintrust_data.dwUnionChoice = WTD_CHOICE_FILE;
    wintrust_data.dwUIChoice = WTD_UI_NONE;

    return WINTRUST_DefaultVerifyAndClose(hwnd, actionID, &wintrust_data);
}

/* Sadly, the function to load the cert for the CERT_CERTIFICATE_ACTION_VERIFY
 * action is not stored in the registry and is located in wintrust, not in
 * cryptdlg along with the rest of the implementation (verified by running the
 * action with a native wintrust.dll.)
 */
static HRESULT WINAPI WINTRUST_CertVerifyObjTrust(CRYPT_PROVIDER_DATA *data)
{
    BOOL ret;

    TRACE("(%p)\n", data);

    if (!data->padwTrustStepErrors)
        return S_FALSE;

    switch (data->pWintrustData->dwUnionChoice)
    {
    case WTD_CHOICE_BLOB:
        if (data->pWintrustData->pBlob &&
         WVT_IS_CBSTRUCT_GT_MEMBEROFFSET(WINTRUST_BLOB_INFO,
         data->pWintrustData->pBlob->cbStruct, pbMemObject) &&
         data->pWintrustData->pBlob->cbMemObject ==
         sizeof(CERT_VERIFY_CERTIFICATE_TRUST) &&
         data->pWintrustData->pBlob->pbMemObject)
        {
            CERT_VERIFY_CERTIFICATE_TRUST *pCert =
             (CERT_VERIFY_CERTIFICATE_TRUST *)
             data->pWintrustData->pBlob->pbMemObject;

            if (pCert->cbSize == sizeof(CERT_VERIFY_CERTIFICATE_TRUST) &&
             pCert->pccert)
            {
                CRYPT_PROVIDER_SGNR signer = { sizeof(signer), { 0 } };
                DWORD i;
                SYSTEMTIME sysTime;

                /* Add a signer with nothing but the time to verify, so we can
                 * add a cert to it
                 */
                GetSystemTime(&sysTime);
                SystemTimeToFileTime(&sysTime, &signer.sftVerifyAsOf);
                ret = data->psPfns->pfnAddSgnr2Chain(data, FALSE, 0, &signer);
                if (!ret)
                    goto error;
                ret = data->psPfns->pfnAddCert2Chain(data, 0, FALSE, 0,
                 pCert->pccert);
                if (!ret)
                    goto error;
                for (i = 0; ret && i < pCert->cRootStores; i++)
                    ret = data->psPfns->pfnAddStore2Chain(data,
                     pCert->rghstoreRoots[i]);
                for (i = 0; ret && i < pCert->cStores; i++)
                    ret = data->psPfns->pfnAddStore2Chain(data,
                     pCert->rghstoreCAs[i]);
                for (i = 0; ret && i < pCert->cTrustStores; i++)
                    ret = data->psPfns->pfnAddStore2Chain(data,
                     pCert->rghstoreTrust[i]);
            }
            else
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                ret = FALSE;
            }
        }
        else
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            ret = FALSE;
        }
        break;
    default:
        FIXME("unimplemented for %ld\n", data->pWintrustData->dwUnionChoice);
        SetLastError(ERROR_INVALID_PARAMETER);
        ret = FALSE;
    }

error:
    if (!ret)
        data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_OBJPROV] =
         GetLastError();
    TRACE("returning %ld (%08lx)\n", ret ? S_OK : S_FALSE,
     data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_OBJPROV]);
    return ret ? S_OK : S_FALSE;
}

static LONG WINTRUST_CertVerify(HWND hwnd, GUID *actionID,
 WINTRUST_DATA *data)
{
    DWORD err = ERROR_SUCCESS, numSteps = 0;
    CRYPT_PROVIDER_DATA *provData;
    BOOL ret;
    struct wintrust_step verifySteps[5];

    TRACE("(%p, %s, %p)\n", hwnd, debugstr_guid(actionID), data);

    provData = WINTRUST_AllocateProviderData();
    if (!provData)
        return ERROR_OUTOFMEMORY;

    ret = WintrustLoadFunctionPointers(actionID, provData->psPfns);
    if (!ret)
    {
        err = GetLastError();
        goto error;
    }
#ifdef __REACTOS__
    WINTRUST_SetUiFunctions(provData);
#endif
    if (!provData->psPfns->pfnObjectTrust)
        provData->psPfns->pfnObjectTrust = WINTRUST_CertVerifyObjTrust;
    /* Not sure why, but native skips the policy check */
    provData->psPfns->pfnCertCheckPolicy = NULL;

    data->hWVTStateData = provData;
    provData->pWintrustData = data;
    provData->hWndParent = hwnd;
    provData->pgActionID = actionID;
    WintrustGetRegPolicyFlags(&provData->dwRegPolicySettings);

    numSteps = WINTRUST_AddTrustStepsFromFunctions(verifySteps,
     provData->psPfns);
    err = WINTRUST_ExecuteSteps(verifySteps, numSteps, provData);
    goto done;

error:
#ifdef __REACTOS__
    WINTRUST_FreeProviderData(provData);
#else
    free(provData->padwTrustStepErrors);
    free(provData->pPDSip);
    free(provData->psPfns);
    free(provData);
#endif

done:
    TRACE("returning %08lx\n", err);
    return err;
}

static LONG WINTRUST_CertVerifyAndClose(HWND hwnd, GUID *actionID,
 WINTRUST_DATA *data)
{
    LONG err;

    TRACE("(%p, %s, %p)\n", hwnd, debugstr_guid(actionID), data);

    err = WINTRUST_CertVerify(hwnd, actionID, data);
    WINTRUST_DefaultClose(hwnd, actionID, data);
    TRACE("returning %08lx\n", err);
    return err;
}

static LONG WINTRUST_CertActionVerify(HWND hwnd, GUID *actionID,
 WINTRUST_DATA *data)
{
    DWORD stateAction;
    LONG err = ERROR_SUCCESS;

    if (WVT_ISINSTRUCT(WINTRUST_DATA, data->cbStruct, dwStateAction))
        stateAction = data->dwStateAction;
    else
    {
        TRACE("no dwStateAction, assuming WTD_STATEACTION_IGNORE\n");
        stateAction = WTD_STATEACTION_IGNORE;
    }
    switch (stateAction)
    {
    case WTD_STATEACTION_IGNORE:
        err = WINTRUST_CertVerifyAndClose(hwnd, actionID, data);
        break;
    case WTD_STATEACTION_VERIFY:
        err = WINTRUST_CertVerify(hwnd, actionID, data);
        break;
    case WTD_STATEACTION_CLOSE:
        err = WINTRUST_DefaultClose(hwnd, actionID, data);
        break;
    default:
        FIXME("unimplemented for %ld\n", data->dwStateAction);
    }
    return err;
}

static void dump_file_info(WINTRUST_FILE_INFO *pFile)
{
    TRACE("%p\n", pFile);
    if (pFile)
    {
        TRACE("cbStruct: %ld\n", pFile->cbStruct);
        TRACE("pcwszFilePath: %s\n", debugstr_w(pFile->pcwszFilePath));
        TRACE("hFile: %p\n", pFile->hFile);
        TRACE("pgKnownSubject: %s\n", debugstr_guid(pFile->pgKnownSubject));
    }
}

static void dump_catalog_info(WINTRUST_CATALOG_INFO *catalog)
{
    TRACE("%p\n", catalog);
    if (catalog)
    {
        TRACE("cbStruct: %ld\n", catalog->cbStruct);
        TRACE("dwCatalogVersion: %ld\n", catalog->dwCatalogVersion);
        TRACE("pcwszCatalogFilePath: %s\n",
         debugstr_w(catalog->pcwszCatalogFilePath));
        TRACE("pcwszMemberTag: %s\n", debugstr_w(catalog->pcwszMemberTag));
        TRACE("pcwszMemberFilePath: %s\n",
         debugstr_w(catalog->pcwszMemberFilePath));
        TRACE("hMemberFile: %p\n", catalog->hMemberFile);
        TRACE("pbCalculatedFileHash: %p\n", catalog->pbCalculatedFileHash);
        TRACE("cbCalculatedFileHash: %ld\n", catalog->cbCalculatedFileHash);
        TRACE("pcCatalogContext: %p\n", catalog->pcCatalogContext);
    }
}

static void dump_blob_info(WINTRUST_BLOB_INFO *blob)
{
    TRACE("%p\n", blob);
    if (blob)
    {
        TRACE("cbStruct: %ld\n", blob->cbStruct);
        TRACE("gSubject: %s\n", debugstr_guid(&blob->gSubject));
        TRACE("pcwszDisplayName: %s\n", debugstr_w(blob->pcwszDisplayName));
        TRACE("cbMemObject: %ld\n", blob->cbMemObject);
        TRACE("pbMemObject: %p\n", blob->pbMemObject);
        TRACE("cbMemSignedMsg: %ld\n", blob->cbMemSignedMsg);
        TRACE("pbMemSignedMsg: %p\n", blob->pbMemSignedMsg);
    }
}

static void dump_sgnr_info(WINTRUST_SGNR_INFO *sgnr)
{
    TRACE("%p\n", sgnr);
    if (sgnr)
    {
        TRACE("cbStruct: %ld\n", sgnr->cbStruct);
        TRACE("pcwszDisplayName: %s\n", debugstr_w(sgnr->pcwszDisplayName));
        TRACE("psSignerInfo: %p\n", sgnr->psSignerInfo);
        TRACE("chStores: %ld\n", sgnr->chStores);
    }
}

static void dump_cert_info(WINTRUST_CERT_INFO *cert)
{
    TRACE("%p\n", cert);
    if (cert)
    {
        TRACE("cbStruct: %ld\n", cert->cbStruct);
        TRACE("pcwszDisplayName: %s\n", debugstr_w(cert->pcwszDisplayName));
        TRACE("psCertContext: %p\n", cert->psCertContext);
        TRACE("chStores: %ld\n", cert->chStores);
        TRACE("dwFlags: %08lx\n", cert->dwFlags);
        TRACE("psftVerifyAsOf: %p\n", cert->psftVerifyAsOf);
    }
}

static void dump_wintrust_data(WINTRUST_DATA *data)
{
    TRACE("%p\n", data);
    if (data)
    {
        TRACE("cbStruct: %ld\n", data->cbStruct);
        TRACE("pPolicyCallbackData: %p\n", data->pPolicyCallbackData);
        TRACE("pSIPClientData: %p\n", data->pSIPClientData);
        TRACE("dwUIChoice: %ld\n", data->dwUIChoice);
        TRACE("fdwRevocationChecks: %08lx\n", data->fdwRevocationChecks);
        TRACE("dwUnionChoice: %ld\n", data->dwUnionChoice);
        switch (data->dwUnionChoice)
        {
        case WTD_CHOICE_FILE:
            dump_file_info(data->pFile);
            break;
        case WTD_CHOICE_CATALOG:
            dump_catalog_info(data->pCatalog);
            break;
        case WTD_CHOICE_BLOB:
            dump_blob_info(data->pBlob);
            break;
        case WTD_CHOICE_SIGNER:
            dump_sgnr_info(data->pSgnr);
            break;
        case WTD_CHOICE_CERT:
            dump_cert_info(data->pCert);
            break;
        }
        TRACE("dwStateAction: %ld\n", data->dwStateAction);
        TRACE("hWVTStateData: %p\n", data->hWVTStateData);
        TRACE("pwszURLReference: %s\n", debugstr_w(data->pwszURLReference));
        TRACE("dwProvFlags: %08lx\n", data->dwProvFlags);
        TRACE("dwUIContext: %ld\n", data->dwUIContext);
    }
}

/***********************************************************************
 *		WinVerifyTrust (WINTRUST.@)
 *
 * Verifies an object by calling the specified trust provider.
 *
 * PARAMS
 *   hwnd       [I] Handle to a caller window.
 *   ActionID   [I] Pointer to a GUID that identifies the action to perform.
 *   ActionData [I] Information used by the trust provider to verify the object.
 *
 * RETURNS
 *   Success: Zero.
 *   Failure: A TRUST_E_* error code.
 *
 * NOTES
 *   Trust providers can be found at:
 *   HKLM\SOFTWARE\Microsoft\Cryptography\Providers\Trust\
 */
LONG WINAPI WinVerifyTrust( HWND hwnd, GUID *ActionID, LPVOID ActionData )
{
    static const GUID unknown = { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,
     0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
    static const GUID published_software = WIN_SPUB_ACTION_PUBLISHED_SOFTWARE;
    static const GUID generic_verify_v2 = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    static const GUID generic_cert_verify = WINTRUST_ACTION_GENERIC_CERT_VERIFY;
    static const GUID generic_chain_verify = WINTRUST_ACTION_GENERIC_CHAIN_VERIFY;
    static const GUID cert_action_verify = CERT_CERTIFICATE_ACTION_VERIFY;
    LONG err = ERROR_SUCCESS;
    WINTRUST_DATA *actionData = ActionData;

    TRACE("(%p, %s, %p)\n", hwnd, debugstr_guid(ActionID), ActionData);
    dump_wintrust_data(ActionData);

    /* Support for known old-style callers: */
    if (IsEqualGUID(ActionID, &published_software))
        err = WINTRUST_PublishedSoftware(hwnd, ActionID, ActionData);
    else if (IsEqualGUID(ActionID, &cert_action_verify))
        err = WINTRUST_CertActionVerify(hwnd, ActionID, ActionData);
    else
    {
        DWORD stateAction;

        /* Check known actions to warn of possible problems */
        if (!IsEqualGUID(ActionID, &unknown) &&
         !IsEqualGUID(ActionID, &generic_verify_v2) &&
         !IsEqualGUID(ActionID, &generic_cert_verify) &&
         !IsEqualGUID(ActionID, &generic_chain_verify))
            WARN("unknown action %s, default behavior may not be right\n",
             debugstr_guid(ActionID));
        if (WVT_ISINSTRUCT(WINTRUST_DATA, actionData->cbStruct, dwStateAction))
            stateAction = actionData->dwStateAction;
        else
        {
            TRACE("no dwStateAction, assuming WTD_STATEACTION_IGNORE\n");
            stateAction = WTD_STATEACTION_IGNORE;
        }
        switch (stateAction)
        {
        case WTD_STATEACTION_IGNORE:
            err = WINTRUST_DefaultVerifyAndClose(hwnd, ActionID, ActionData);
            break;
        case WTD_STATEACTION_VERIFY:
            err = WINTRUST_DefaultVerify(hwnd, ActionID, ActionData);
            break;
        case WTD_STATEACTION_CLOSE:
            err = WINTRUST_DefaultClose(hwnd, ActionID, ActionData);
            break;
        default:
            FIXME("unimplemented for %ld\n", actionData->dwStateAction);
        }
    }

    if (err) SetLastError(err);
    TRACE("returning %08lx\n", err);
    return err;
}

/***********************************************************************
 *		WinVerifyTrustEx (WINTRUST.@)
 */
HRESULT WINAPI WinVerifyTrustEx( HWND hwnd, GUID *ActionID,
 WINTRUST_DATA* ActionData )
{
    return WinVerifyTrust(hwnd, ActionID, ActionData);
}

/***********************************************************************
 *		WTHelperGetProvSignerFromChain (WINTRUST.@)
 */
CRYPT_PROVIDER_SGNR * WINAPI WTHelperGetProvSignerFromChain(
 CRYPT_PROVIDER_DATA *pProvData, DWORD idxSigner, BOOL fCounterSigner,
 DWORD idxCounterSigner)
{
    CRYPT_PROVIDER_SGNR *sgnr;

    TRACE("(%p %ld %d %ld)\n", pProvData, idxSigner, fCounterSigner,
     idxCounterSigner);

    if (idxSigner >= pProvData->csSigners || !pProvData->pasSigners)
        return NULL;
    sgnr = &pProvData->pasSigners[idxSigner];
    if (fCounterSigner)
    {
        if (idxCounterSigner >= sgnr->csCounterSigners ||
         !sgnr->pasCounterSigners)
            return NULL;
        sgnr = &sgnr->pasCounterSigners[idxCounterSigner];
    }
    TRACE("returning %p\n", sgnr);
    return sgnr;
}

/***********************************************************************
 *		WTHelperGetProvCertFromChain (WINTRUST.@)
 */
CRYPT_PROVIDER_CERT * WINAPI WTHelperGetProvCertFromChain(
 CRYPT_PROVIDER_SGNR *pSgnr, DWORD idxCert)
{
    CRYPT_PROVIDER_CERT *cert;

    TRACE("(%p %ld)\n", pSgnr, idxCert);

    if (idxCert >= pSgnr->csCertChain || !pSgnr->pasCertChain)
        return NULL;
    cert = &pSgnr->pasCertChain[idxCert];
    TRACE("returning %p\n", cert);
    return cert;
}

CRYPT_PROVIDER_PRIVDATA *WINAPI WTHelperGetProvPrivateDataFromChain(
 CRYPT_PROVIDER_DATA* pProvData,
 GUID* pgProviderID)
{
    CRYPT_PROVIDER_PRIVDATA *privdata = NULL;
    DWORD i;

    TRACE("(%p, %s)\n", pProvData, debugstr_guid(pgProviderID));

    for (i = 0; i < pProvData->csProvPrivData; i++)
        if (IsEqualGUID(pgProviderID, &pProvData->pasProvPrivData[i].gProviderID))
        {
            privdata = &pProvData->pasProvPrivData[i];
            break;
        }

    return privdata;
}

/***********************************************************************
 *		WTHelperProvDataFromStateData (WINTRUST.@)
 */
CRYPT_PROVIDER_DATA * WINAPI WTHelperProvDataFromStateData(HANDLE hStateData)
{
    TRACE("%p\n", hStateData);
    return hStateData;
}

/***********************************************************************
 *		WTHelperGetFileName(WINTRUST.@)
 */
LPCWSTR WINAPI WTHelperGetFileName(WINTRUST_DATA *data)
{
    TRACE("%p\n",data);
    if (data->dwUnionChoice == WTD_CHOICE_FILE)
        return data->pFile->pcwszFilePath;
    else
        return NULL;
}

/***********************************************************************
 *		WTHelperGetFileHandle(WINTRUST.@)
 */
HANDLE WINAPI WTHelperGetFileHandle(WINTRUST_DATA *data)
{
    TRACE("%p\n",data);
    if (data->dwUnionChoice == WTD_CHOICE_FILE)
        return data->pFile->hFile;
    else
        return INVALID_HANDLE_VALUE;
}

static BOOL WINAPI WINTRUST_enumUsages(PCCRYPT_OID_INFO pInfo, void *pvArg)
{
    PCCRYPT_OID_INFO **usages = pvArg;
    DWORD cUsages;
    BOOL ret;

    if (!*usages)
    {
        cUsages = 0;
        *usages = malloc(2 * sizeof(PCCRYPT_OID_INFO));
    }
    else
    {
        PCCRYPT_OID_INFO *ptr;

        /* Count the existing usages.
         * FIXME: make sure the new usage doesn't duplicate any in the list?
         */
        for (cUsages = 0, ptr = *usages; *ptr; ptr++, cUsages++)
            ;
        *usages = realloc(*usages, (cUsages + 2) * sizeof(PCCRYPT_OID_INFO));
    }
    if (*usages)
    {
        (*usages)[cUsages] = pInfo;
        (*usages)[cUsages + 1] = NULL;
        ret = TRUE;
    }
    else
    {
        SetLastError(ERROR_OUTOFMEMORY);
        ret = FALSE;
    }
    return ret;
}

/***********************************************************************
 *		WTHelperGetKnownUsages(WINTRUST.@)
 *
 * Enumerates the known enhanced key usages as an array of PCCRYPT_OID_INFOs.
 *
 * PARAMS
 *  action      [In]     1 => allocate and return known usages, 2 => free previously
 *                       allocated usages.
 *  usages      [In/Out] If action == 1, *usages is set to an array of
 *                       PCCRYPT_OID_INFO *.  The array is terminated with a NULL
 *                       pointer.
 *                       If action == 2, *usages is freed.
 *
 * RETURNS
 *  TRUE on success, FALSE on failure.
 */
BOOL WINAPI WTHelperGetKnownUsages(DWORD action, PCCRYPT_OID_INFO **usages)
{
    BOOL ret;

    TRACE("(%ld, %p)\n", action, usages);

    if (!usages)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (action == 1)
    {
        *usages = NULL;
        ret = CryptEnumOIDInfo(CRYPT_ENHKEY_USAGE_OID_GROUP_ID, 0, usages,
         WINTRUST_enumUsages);
    }
    else if (action == 2)
    {
        free(*usages);
        *usages = NULL;
        ret = TRUE;
    }
    else
    {
        WARN("unknown action %ld\n", action);
        SetLastError(ERROR_INVALID_PARAMETER);
        ret = FALSE;
    }
    return ret;
}

static const WCHAR Software_Publishing[] = {
 'S','o','f','t','w','a','r','e','\\',
 'M','i','c','r','o','s','o','f','t','\\',
 'W','i','n','d','o','w','s','\\',
 'C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\',
 'W','i','n','t','r','u','s','t','\\',
 'T','r','u','s','t',' ','P','r','o','v','i','d','e','r','s','\\',
 'S','o','f','t','w','a','r','e',' ',
 'P','u','b','l','i','s','h','i','n','g',0 };
static const WCHAR State[] = { 'S','t','a','t','e',0 };

/***********************************************************************
 *		WintrustGetRegPolicyFlags (WINTRUST.@)
 */
void WINAPI WintrustGetRegPolicyFlags( DWORD* pdwPolicyFlags )
{
    HKEY key;
    LONG r;

    TRACE("%p\n", pdwPolicyFlags);

    *pdwPolicyFlags = 0;
    r = RegCreateKeyExW(HKEY_CURRENT_USER, Software_Publishing, 0, NULL, 0,
     KEY_READ, NULL, &key, NULL);
    if (!r)
    {
        DWORD size = sizeof(DWORD);

        r = RegQueryValueExW(key, State, NULL, NULL, (LPBYTE)pdwPolicyFlags,
         &size);
        RegCloseKey(key);
        if (r)
        {
            /* Failed to query, create and return default value */
            *pdwPolicyFlags = WTPF_IGNOREREVOCATIONONTS |
             WTPF_OFFLINEOKNBU_COM |
             WTPF_OFFLINEOKNBU_IND |
             WTPF_OFFLINEOK_COM |
             WTPF_OFFLINEOK_IND;
            WintrustSetRegPolicyFlags(*pdwPolicyFlags);
        }
    }
}

/***********************************************************************
 *		WintrustSetRegPolicyFlags (WINTRUST.@)
 */
BOOL WINAPI WintrustSetRegPolicyFlags( DWORD dwPolicyFlags)
{
    HKEY key;
    LONG r;

    TRACE("%lx\n", dwPolicyFlags);

    r = RegCreateKeyExW(HKEY_CURRENT_USER, Software_Publishing, 0,
     NULL, 0, KEY_WRITE, NULL, &key, NULL);
    if (!r)
    {
        r = RegSetValueExW(key, State, 0, REG_DWORD, (LPBYTE)&dwPolicyFlags,
         sizeof(DWORD));
        RegCloseKey(key);
    }
    if (r) SetLastError(r);
    return r == ERROR_SUCCESS;
}

/* Utility functions */

BOOL WINAPI WINTRUST_AddStore(CRYPT_PROVIDER_DATA *data, HCERTSTORE store)
{
    BOOL ret = FALSE;

    TRACE("(%p, %p)\n", data, store);

    data->pahStores = realloc(data->pahStores, (data->chStores + 1) * sizeof(HCERTSTORE));
    if (data->pahStores)
    {
        data->pahStores[data->chStores++] = CertDuplicateStore(store);
        ret = TRUE;
    }
    else
        SetLastError(ERROR_OUTOFMEMORY);
    return ret;
}

BOOL WINAPI WINTRUST_AddSgnr(CRYPT_PROVIDER_DATA *data,
 BOOL fCounterSigner, DWORD idxSigner, CRYPT_PROVIDER_SGNR *sgnr)
{
    BOOL ret = FALSE;

    TRACE("(%p, %d, %ld, %p)\n", data, fCounterSigner, idxSigner, sgnr);

    if (sgnr->cbStruct > sizeof(CRYPT_PROVIDER_SGNR))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
#ifdef __REACTOS__
    if (fCounterSigner)
    {
        CRYPT_PROVIDER_SGNR *signer, *counters;

        if (idxSigner >= data->csSigners)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        signer = &data->pasSigners[idxSigner];
        counters = realloc(signer->pasCounterSigners, (signer->csCounterSigners + 1) * sizeof(*counters));
        if (!counters)
        {
            SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        signer->pasCounterSigners = counters;
        memset(&counters[signer->csCounterSigners], 0, sizeof(*counters));
        memcpy(&counters[signer->csCounterSigners], sgnr, sgnr->cbStruct);
        signer->csCounterSigners++;
        return TRUE;
    }
    if (sgnr->cbStruct != sizeof(CRYPT_PROVIDER_SGNR))
        idxSigner = data->csSigners;
#else
    if (fCounterSigner)
    {
        FIXME("unimplemented for counter signers\n");
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
#endif
    data->pasSigners = realloc(data->pasSigners,
     (data->csSigners + 1) * sizeof(CRYPT_PROVIDER_SGNR));
    if (data->pasSigners)
    {
        if (idxSigner < data->csSigners)
            memmove(&data->pasSigners[idxSigner],
             &data->pasSigners[idxSigner + 1],
             (data->csSigners - idxSigner) * sizeof(CRYPT_PROVIDER_SGNR));
        ret = TRUE;
        if (sgnr->cbStruct == sizeof(CRYPT_PROVIDER_SGNR))
        {
            /* The PSDK says psSigner should be allocated using pfnAlloc, but
             * it doesn't say anything about ownership.  Since callers are
             * internal, assume ownership is passed, and just store the
             * pointer.
             */
            memcpy(&data->pasSigners[idxSigner], sgnr,
             sizeof(CRYPT_PROVIDER_SGNR));
        }
        else
#ifdef __REACTOS__
        {
            memset(&data->pasSigners[idxSigner], 0,
             sizeof(CRYPT_PROVIDER_SGNR));
            memcpy(&data->pasSigners[idxSigner], sgnr, sgnr->cbStruct);
        }
#else
            memset(&data->pasSigners[idxSigner], 0,
             sizeof(CRYPT_PROVIDER_SGNR));
#endif
        data->csSigners++;
    }
    else
        SetLastError(ERROR_OUTOFMEMORY);
    return ret;
}

BOOL WINAPI WINTRUST_AddCert(CRYPT_PROVIDER_DATA *data, DWORD idxSigner,
 BOOL fCounterSigner, DWORD idxCounterSigner, PCCERT_CONTEXT pCert2Add)
{
#ifndef __REACTOS__
    BOOL ret = FALSE;
#endif

    TRACE("(%p, %ld, %d, %ld, %p)\n", data, idxSigner, fCounterSigner,
     idxSigner, pCert2Add);

#ifdef __REACTOS__
    {
        CRYPT_PROVIDER_SGNR *sgnr;
        CRYPT_PROVIDER_CERT *chain;

        if (idxSigner >= data->csSigners || (fCounterSigner &&
         idxCounterSigner >= data->pasSigners[idxSigner].csCounterSigners))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        sgnr = fCounterSigner ? &data->pasSigners[idxSigner].pasCounterSigners[idxCounterSigner] :
         &data->pasSigners[idxSigner];
        chain = realloc(sgnr->pasCertChain, (sgnr->csCertChain + 1) * sizeof(*chain));
        if (!chain)
        {
            SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        sgnr->pasCertChain = chain;
        memset(&chain[sgnr->csCertChain], 0, sizeof(*chain));
        chain[sgnr->csCertChain].cbStruct = sizeof(CRYPT_PROVIDER_CERT);
        chain[sgnr->csCertChain].pCert = CertDuplicateCertificateContext(pCert2Add);
        sgnr->csCertChain++;
        return TRUE;
    }
#else
    if (fCounterSigner)
    {
        FIXME("unimplemented for counter signers\n");
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    data->pasSigners[idxSigner].pasCertChain =
     realloc(data->pasSigners[idxSigner].pasCertChain,
     (data->pasSigners[idxSigner].csCertChain + 1) *
     sizeof(CRYPT_PROVIDER_CERT));
    if (data->pasSigners[idxSigner].pasCertChain)
    {
        CRYPT_PROVIDER_CERT *cert = &data->pasSigners[idxSigner].pasCertChain[
         data->pasSigners[idxSigner].csCertChain];

        memset(cert, 0, sizeof(*cert));
        cert->cbStruct = sizeof(CRYPT_PROVIDER_CERT);
        cert->pCert = CertDuplicateCertificateContext(pCert2Add);
        data->pasSigners[idxSigner].csCertChain++;
        ret = TRUE;
    }
    else
        SetLastError(ERROR_OUTOFMEMORY);
    return ret;
#endif
}

BOOL WINAPI WINTRUST_AddPrivData(CRYPT_PROVIDER_DATA *data,
 CRYPT_PROVIDER_PRIVDATA *pPrivData2Add)
{
    BOOL ret = FALSE;

    TRACE("(%p, %p)\n", data, pPrivData2Add);

    if (pPrivData2Add->cbStruct > sizeof(CRYPT_PROVIDER_PRIVDATA))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        WARN("invalid struct size\n");
        return FALSE;
    }
    data->pasProvPrivData = realloc(data->pasProvPrivData,
     (data->csProvPrivData + 1) * sizeof(*data->pasProvPrivData));
    if (data->pasProvPrivData)
    {
        DWORD i;

        for (i = 0; i < data->csProvPrivData; i++)
            if (IsEqualGUID(&pPrivData2Add->gProviderID, &data->pasProvPrivData[i]))
                break;

        data->pasProvPrivData[i] = *pPrivData2Add;
        if (i == data->csProvPrivData)
            data->csProvPrivData++;
    }
    else
        SetLastError(ERROR_OUTOFMEMORY);
    return ret;
}

/***********************************************************************
 *		OpenPersonalTrustDBDialog (WINTRUST.@)
 *
 * Opens the certificate manager dialog, showing only the stores that
 * contain trusted software publishers.
 *
 * PARAMS
 *  hwnd [I] handle of parent window
 *
 * RETURNS
 *  TRUE if the dialog could be opened, FALSE if not.
 */
BOOL WINAPI OpenPersonalTrustDBDialog(HWND hwnd)
{
#ifdef __REACTOS__
    return OpenPersonalTrustDBDialogEx(hwnd, 0, NULL);
#else
    CRYPTUI_CERT_MGR_STRUCT uiCertMgr;

    uiCertMgr.dwSize = sizeof(uiCertMgr);
    uiCertMgr.hwndParent = hwnd;
    uiCertMgr.dwFlags = CRYPTUI_CERT_MGR_PUBLISHER_TAB;
    uiCertMgr.pwszTitle = NULL;
    uiCertMgr.pszInitUsageOID = NULL;
    return CryptUIDlgCertMgr(&uiCertMgr);
#endif
}

#ifdef __REACTOS__
/***********************************************************************
 *		OpenPersonalTrustDBDialogEx (WINTRUST.@)
 */
BOOL WINAPI OpenPersonalTrustDBDialogEx(HWND hwnd, DWORD flags, PVOID *reserved)
{
    CRYPTUI_CERT_MGR_STRUCT uiCertMgr;

    TRACE("(%p %#lx %p)\n", hwnd, flags, reserved);

    if (flags & WT_TRUSTDBDIALOG_NO_UI_FLAG)
        return TRUE;

    uiCertMgr.dwSize = sizeof(uiCertMgr);
    uiCertMgr.hwndParent = hwnd;
    uiCertMgr.dwFlags = CRYPTUI_CERT_MGR_PUBLISHER_TAB;
    if (flags & WT_TRUSTDBDIALOG_ONLY_PUB_TAB_FLAG)
        uiCertMgr.dwFlags |= CRYPTUI_CERT_MGR_SINGLE_TAB_FLAG;
    uiCertMgr.pwszTitle = NULL;
    uiCertMgr.pszInitUsageOID = NULL;
    return CryptUIDlgCertMgr(&uiCertMgr);
}

/***********************************************************************
 *		WTHelperCertIsSelfSigned (WINTRUST.@)
 */
BOOL WINAPI WTHelperCertIsSelfSigned(DWORD dwEncoding, CERT_INFO *pCert)
{
    TRACE("(%#lx %p)\n", dwEncoding, pCert);

    return CertCompareCertificateName(dwEncoding, &pCert->Subject, &pCert->Issuer);
}

static BOOL WINTRUST_AuthorityNamesIssuer(PCCERT_CONTEXT child, PCCERT_CONTEXT issuer)
{
    CERT_AUTHORITY_KEY_ID2_INFO *info2;
    CERT_AUTHORITY_KEY_ID_INFO *info;
    PCERT_EXTENSION ext;
    BOOL ret = FALSE;
    DWORD size, i;

    if ((ext = CertFindExtension(szOID_AUTHORITY_KEY_IDENTIFIER, child->pCertInfo->cExtension,
                                 child->pCertInfo->rgExtension)) &&
        CryptDecodeObjectEx(child->dwCertEncodingType, X509_AUTHORITY_KEY_ID, ext->Value.pbData, ext->Value.cbData,
                            CRYPT_DECODE_ALLOC_FLAG, NULL, &info, &size))
    {
        ret = info->CertIssuer.cbData && info->CertSerialNumber.cbData &&
              CertCompareCertificateName(child->dwCertEncodingType, &info->CertIssuer, &issuer->pCertInfo->Issuer) &&
              CertCompareIntegerBlob(&info->CertSerialNumber, &issuer->pCertInfo->SerialNumber);
        LocalFree(info);
    }
    if (!ret && (ext = CertFindExtension(szOID_AUTHORITY_KEY_IDENTIFIER2, child->pCertInfo->cExtension,
                                         child->pCertInfo->rgExtension)) &&
        CryptDecodeObjectEx(child->dwCertEncodingType, X509_AUTHORITY_KEY_ID2, ext->Value.pbData, ext->Value.cbData,
                            CRYPT_DECODE_ALLOC_FLAG, NULL, &info2, &size))
    {
        if (info2->AuthorityCertSerialNumber.cbData &&
            CertCompareIntegerBlob(&info2->AuthorityCertSerialNumber, &issuer->pCertInfo->SerialNumber))
        {
            for (i = 0; !ret && i < info2->AuthorityCertIssuer.cAltEntry; i++)
            {
                ret = info2->AuthorityCertIssuer.rgAltEntry[i].dwAltNameChoice == CERT_ALT_NAME_DIRECTORY_NAME &&
                      CertCompareCertificateName(child->dwCertEncodingType,
                                                 &info2->AuthorityCertIssuer.rgAltEntry[i].DirectoryName,
                                                 &issuer->pCertInfo->Issuer);
            }
        }
        LocalFree(info2);
    }
    return ret;
}

static DWORD WINTRUST_IssuerConfidence(PCCERT_CONTEXT child, PCCERT_CONTEXT issuer, FILETIME *time)
{
    DWORD confidence = 0;

    if (CryptVerifyCertificateSignatureEx(0, child->dwCertEncodingType, CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT,
                                          (void *)child, CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT, (void *)issuer, 0, NULL))
        confidence |= CERT_CONFIDENCE_SIG;
    if (!CertVerifyTimeValidity(time, issuer->pCertInfo))
        confidence |= CERT_CONFIDENCE_TIME;
    if (CertVerifyValidityNesting(child->pCertInfo, issuer->pCertInfo))
        confidence |= CERT_CONFIDENCE_TIMENEST;
    if (WINTRUST_AuthorityNamesIssuer(child, issuer))
    {
        confidence |= CERT_CONFIDENCE_AUTHIDEXT;
        if (confidence & CERT_CONFIDENCE_SIG)
            confidence |= CERT_CONFIDENCE_HYGIENE;
    }
    return confidence;
}

/***********************************************************************
 *		WTHelperCertFindIssuerCertificate (WINTRUST.@)
 */
PCCERT_CONTEXT WINAPI WTHelperCertFindIssuerCertificate(PCCERT_CONTEXT pChildContext, DWORD chStores,
                                                        HCERTSTORE *pahStores, FILETIME *psftVerifyAsOf,
                                                        DWORD dwEncoding, DWORD *pdwConfidence, DWORD *pdwError)
{
    PCCERT_CONTEXT best = NULL, candidate;
    DWORD i, confidence, best_confidence = 0;

    TRACE("(%p %lu %p %p %#lx %p %p)\n", pChildContext, chStores, pahStores, psftVerifyAsOf, dwEncoding,
          pdwConfidence, pdwError);

    if (!chStores || !pahStores)
        return NULL;

    for (i = 0; i < chStores; i++)
    {
        candidate = NULL;
        while ((candidate = CertFindCertificateInStore(pahStores[i], dwEncoding, 0, CERT_FIND_SUBJECT_NAME,
                                                       &pChildContext->pCertInfo->Issuer, candidate)))
        {
            if (CertCompareCertificate(dwEncoding, candidate->pCertInfo, pChildContext->pCertInfo))
                continue;
            confidence = WINTRUST_IssuerConfidence(pChildContext, candidate, psftVerifyAsOf);
            if (best && confidence <= best_confidence)
                continue;
            CertFreeCertificateContext(best);
            best = CertDuplicateCertificateContext(candidate);
            best_confidence = confidence;
        }
    }
    if (pdwConfidence)
        *pdwConfidence = best_confidence;
    if (pdwError)
        *pdwError = best && !(best_confidence & CERT_CONFIDENCE_SIG) ? TRUST_E_CERT_SIGNATURE : ERROR_SUCCESS;
    return best;
}

static const WCHAR *const WINTRUST_KnownStores[] = { L"Root", L"Trust", L"CA", L"My", L"TrustedPublisher" };

static BOOL WINTRUST_OpenKnownStore(HCRYPTPROV hProv, DWORD index, HCERTSTORE *store)
{
    *store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, hProv, CERT_SYSTEM_STORE_CURRENT_USER,
                           WINTRUST_KnownStores[index]);
    return *store != NULL;
}

/***********************************************************************
 *		TrustOpenStores (WINTRUST.@)
 */
BOOL WINAPI TrustOpenStores(HCRYPTPROV hProv, DWORD *pchStores, HCERTSTORE *pahStores, DWORD dwFlags)
{
    DWORD i;

    TRACE("(%Ix %p %p %#lx)\n", hProv, pchStores, pahStores, dwFlags);

    if (!pchStores || dwFlags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!pahStores || *pchStores < ARRAY_SIZE(WINTRUST_KnownStores))
    {
        BOOL ret = !pahStores;

        *pchStores = ARRAY_SIZE(WINTRUST_KnownStores);
        SetLastError(ERROR_MORE_DATA);
        return ret;
    }
    for (i = 0; i < ARRAY_SIZE(WINTRUST_KnownStores); i++)
    {
        if (!WINTRUST_OpenKnownStore(hProv, i, &pahStores[i]))
        {
            while (i)
                CertCloseStore(pahStores[--i], 0);
            return FALSE;
        }
    }
    *pchStores = ARRAY_SIZE(WINTRUST_KnownStores);
    return TRUE;
}

/***********************************************************************
 *		WTHelperOpenKnownStores (WINTRUST.@)
 */
BOOL WINAPI WTHelperOpenKnownStores(CRYPT_PROVIDER_DATA *pProvData)
{
    HCERTSTORE store;
    BOOL ret = TRUE;
    DWORD i;

    TRACE("(%p)\n", pProvData);

    if (!pProvData)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (i = 0; ret && i < ARRAY_SIZE(WINTRUST_KnownStores); i++)
    {
        if (!(ret = WINTRUST_OpenKnownStore(pProvData->hProv, i, &store)))
            break;
        ret = pProvData->psPfns->pfnAddStore2Chain(pProvData, store);
        CertCloseStore(store, 0);
    }
    return ret;
}

/***********************************************************************
 *		WTHelperIsInRootStore (WINTRUST.@)
 */
BOOL WINAPI WTHelperIsInRootStore(CRYPT_PROVIDER_DATA *pProvData, PCCERT_CONTEXT pCertContext)
{
    PCCERT_CONTEXT found;

    TRACE("(%p %p)\n", pProvData, pCertContext);

    if (!pProvData || !pCertContext || !pProvData->chStores)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!(found = CertFindCertificateInStore(pProvData->pahStores[0], pCertContext->dwCertEncodingType, 0,
                                             CERT_FIND_EXISTING, pCertContext, NULL)))
        return FALSE;
    CertFreeCertificateContext(found);
    return TRUE;
}

/***********************************************************************
 *		WTHelperCheckCertUsage (WINTRUST.@)
 */
BOOL WINAPI WTHelperCheckCertUsage(PCCERT_CONTEXT pCertContext, LPCSTR pszRequestedUsageOID)
{
    CERT_ENHKEY_USAGE *usage;
    DWORD size = 0, i;
    BOOL ret = FALSE;

    TRACE("(%p %s)\n", pCertContext, debugstr_a(pszRequestedUsageOID));

    if (!pCertContext || !pszRequestedUsageOID)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return TRUE;
    }
    if (!CertGetEnhancedKeyUsage(pCertContext, 0, NULL, &size) || !(usage = malloc(size)))
        return FALSE;
    if (CertGetEnhancedKeyUsage(pCertContext, 0, usage, &size))
    {
        ret = !usage->cUsageIdentifier && GetLastError() == CRYPT_E_NOT_FOUND;
        for (i = 0; !ret && i < usage->cUsageIdentifier; i++)
            ret = !strcmp(usage->rgpszUsageIdentifier[i], pszRequestedUsageOID);
    }
    free(usage);
    if (!ret)
        SetLastError(CERT_E_WRONG_USAGE);
    return ret;
}

static BOOL WINTRUST_ChainIsMicrosoft(PCCERT_CHAIN_CONTEXT chain)
{
    CERT_CHAIN_POLICY_STATUS status = { sizeof(status) };
    CERT_CHAIN_POLICY_PARA para = { sizeof(para) };

    return chain && CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT, chain, &para, &status) &&
           !status.dwError;
}

/***********************************************************************
 *		WTHelperIsChainedToMicrosoft (WINTRUST.@)
 */
BOOL WINAPI WTHelperIsChainedToMicrosoft(PCCERT_CONTEXT pCertContext, HCERTSTORE hStore, DWORD dwFlags)
{
    CERT_CHAIN_PARA para = { sizeof(para) };
    PCCERT_CHAIN_CONTEXT chain;
    BOOL ret;

    TRACE("(%p %p %#lx)\n", pCertContext, hStore, dwFlags);

    if (!pCertContext || !hStore)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* TODO: INFO_NEEDED: the meaning of dwFlags; 0, 1, 2 and ~0 give the same result on Windows 11 */
    if (!CertGetCertificateChain(NULL, pCertContext, NULL, hStore, &para, 0, NULL, &chain))
        return FALSE;
    ret = WINTRUST_ChainIsMicrosoft(chain);
    CertFreeCertificateChain(chain);
    return ret;
}

/***********************************************************************
 *		WTHelperIsChainedToMicrosoftFromStateData (WINTRUST.@)
 */
BOOL WINAPI WTHelperIsChainedToMicrosoftFromStateData(HANDLE hStateData, DWORD dwFlags)
{
    CRYPT_PROVIDER_DATA *data = hStateData;

    TRACE("(%p %#lx)\n", hStateData, dwFlags);

    if (!data)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return data->csSigners && WINTRUST_ChainIsMicrosoft(data->pasSigners[0].pChainContext);
}

/***********************************************************************
 *		GetAuthenticodeSha256Hash (WINTRUST.@)
 */
HRESULT WINAPI GetAuthenticodeSha256Hash(PVOID pvParam1, PVOID pvParam2, PVOID pvParam3)
{
    TRACE("(%p %p %p)\n", pvParam1, pvParam2, pvParam3);

    /* TODO: INFO_NEEDED: the parameters Windows 11 accepts; every file, mapping, module and process handle tried fails */
    SetLastError(ERROR_INVALID_HANDLE);
    return E_INVALIDARG;
}

/***********************************************************************
 *		ConfigCiPackageFamilyNameCheck (WINTRUST.@)
 */
DWORD WINAPI ConfigCiPackageFamilyNameCheck(PVOID pvParam1, PVOID pvParam2, PVOID pvParam3)
{
    TRACE("(%p %p %p)\n", pvParam1, pvParam2, pvParam3);

    /* TODO: INFO_NEEDED: the meaning of the result; Windows 11 returns 0x10000000 for every package family name tried */
    return 0x10000000;
}

/***********************************************************************
 *		ConfigCiFinalPolicy (WINTRUST.@)
 */
HRESULT WINAPI ConfigCiFinalPolicy(CRYPT_PROVIDER_DATA *pProvData)
{
    static const GUID ConfigCiAction = { 0x6078065b, 0x8f22, 0x4b13, { 0xbd,0x9b,0x5b,0x76,0x27,0x76,0xf3,0x86 } };

    TRACE("(%p)\n", pProvData);

    if (!pProvData || !pProvData->pgActionID || !IsEqualGUID(pProvData->pgActionID, &ConfigCiAction))
        return E_INVALIDARG;
    /* TODO: INFO_NEEDED: Windows 11 asks the active code integrity policy; LiberNT enforces none and allows the subject */
    return S_OK;
}

/***********************************************************************
 *		SrpCheckSmartlockerEAandProcessToken (WINTRUST.@)
 */
BOOL WINAPI SrpCheckSmartlockerEAandProcessToken(HANDLE hFile, HANDLE hToken, PVOID pvResult)
{
    TRACE("(%p %p %p)\n", hFile, hToken, pvResult);

    /* TODO: INFO_NEEDED: the result for a file that carries the SmartLocker extended attribute */
    return FALSE;
}

/***********************************************************************
 *		WTValidateBioSignaturePolicy (WINTRUST.@)
 */
HRESULT WINAPI WTValidateBioSignaturePolicy(PVOID pvPolicy, BOOLEAN *pfValid)
{
    TRACE("(%p %p)\n", pvPolicy, pfValid);

    *pfValid = FALSE;
    /* TODO: INFO_NEEDED: the policy structure Windows 11 accepts; a zeroed one gives E_INVALIDARG */
    return E_INVALIDARG;
}

/***********************************************************************
 *		WTGetBioSignatureInfo (WINTRUST.@)
 */
HRESULT WINAPI WTGetBioSignatureInfo(PVOID pvParam1, PVOID pvParam2, PVOID pvParam3, PVOID pvParam4, PVOID pvParam5,
                                     PVOID pvParam6)
{
    TRACE("(%p %p %p %p %p %p)\n", pvParam1, pvParam2, pvParam3, pvParam4, pvParam5, pvParam6);

    /* TODO: INFO_NEEDED: the parameters Windows 11 accepts; every set tried gives E_INVALIDARG */
    return E_INVALIDARG;
}

/***********************************************************************
 *		WTGetPluginSignatureInfo (WINTRUST.@)
 */
HRESULT WINAPI WTGetPluginSignatureInfo(PVOID pvParam1, PVOID pvParam2, PVOID pvParam3, PVOID pvParam4, PVOID pvParam5,
                                        PVOID pvParam6)
{
    TRACE("(%p %p %p %p %p %p)\n", pvParam1, pvParam2, pvParam3, pvParam4, pvParam5, pvParam6);

    /* TODO: INFO_NEEDED: the parameters Windows 11 accepts; every set tried gives E_INVALIDARG */
    return E_INVALIDARG;
}

/***********************************************************************
 *		WTIsFirstConfigCiResultPreferred (WINTRUST.@)
 */
HRESULT WINAPI WTIsFirstConfigCiResultPreferred(PVOID pvParam1, PVOID pvParam2, PVOID pvParam3)
{
    TRACE("(%p %p %p)\n", pvParam1, pvParam2, pvParam3);

    if (!pvParam1 || !pvParam2 || !pvParam3)
        return E_INVALIDARG;
    /* TODO: INFO_NEEDED: the comparison; Windows 11 prefers the first result for every pair tried */
    *(BOOLEAN *)pvParam3 = TRUE;
    return S_OK;
}

/***********************************************************************
 *		WTLogConfigCiSignerEvent (WINTRUST.@)
 */
HRESULT WINAPI WTLogConfigCiSignerEvent(PVOID pvParam1, PVOID pvParam2, PVOID pvParam3, PVOID pvParam4)
{
    TRACE("(%p %p %p %p)\n", pvParam1, pvParam2, pvParam3, pvParam4);

    if (!pvParam1)
        return E_INVALIDARG;
    /* TODO: INFO_NEEDED: the event Windows 11 logs; only NULL parameters were measured */
    return S_OK;
}

/***********************************************************************
 *		TrustDecode (WINTRUST.@)
 */
BOOL WINAPI TrustDecode(DWORD dwModuleId, BYTE **ppbRet, DWORD *pcbRet, DWORD dwErrorId, DWORD dwEncoding,
                        LPCSTR pcszOID, const void *pvData, DWORD cbData, DWORD dwFlags)
{
    TRACE("(%lu %p %p %#lx %#lx %s %p %lu %#lx)\n", dwModuleId, ppbRet, pcbRet, dwErrorId, dwEncoding,
          debugstr_a(pcszOID), pvData, cbData, dwFlags);

    if (!ppbRet || !pcbRet)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (CryptDecodeObjectEx(dwEncoding, pcszOID, pvData, cbData, dwFlags | CRYPT_DECODE_ALLOC_FLAG, NULL, ppbRet,
                            pcbRet))
        return TRUE;
    *ppbRet = NULL;
    *pcbRet = 0;
    return FALSE;
}

/***********************************************************************
 *		TrustFreeDecode (WINTRUST.@)
 */
VOID WINAPI TrustFreeDecode(DWORD dwModuleId, BYTE **pbAllocated)
{
    TRACE("(%lu %p)\n", dwModuleId, pbAllocated);

    if (!pbAllocated)
        return;
    LocalFree(*pbAllocated);
    *pbAllocated = NULL;
}

/***********************************************************************
 *		WTHelperGetAgencyInfo (WINTRUST.@)
 */
BOOL WINAPI WTHelperGetAgencyInfo(PCCERT_CONTEXT pCert, DWORD *pcbAgencyInfo, SPC_SP_AGENCY_INFO *pAgencyInfo)
{
    PCERT_EXTENSION ext;
    DWORD needed = 0;

    TRACE("(%p %p %p)\n", pCert, pcbAgencyInfo, pAgencyInfo);

    if (!pCert || !pcbAgencyInfo)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!(ext = CertFindExtension(SPC_SP_AGENCY_INFO_OBJID, pCert->pCertInfo->cExtension,
                                  pCert->pCertInfo->rgExtension)))
    {
        *pcbAgencyInfo = 0;
        return FALSE;
    }
    if (!CryptDecodeObject(X509_ASN_ENCODING, SPC_SP_AGENCY_INFO_OBJID, ext->Value.pbData, ext->Value.cbData, 0, NULL,
                           &needed))
        return FALSE;
    if (!pAgencyInfo || *pcbAgencyInfo < needed)
    {
        /* FIXME: Windows 11 decodes into a non-NULL buffer whatever size the caller gave; LiberNT refuses */
        *pcbAgencyInfo = needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    *pcbAgencyInfo = needed;
    return CryptDecodeObject(X509_ASN_ENCODING, SPC_SP_AGENCY_INFO_OBJID, ext->Value.pbData, ext->Value.cbData, 0,
                             pAgencyInfo, pcbAgencyInfo);
}

/***********************************************************************
 *		AddPersonalTrustDBPages (WINTRUST.@)
 */
BOOL WINAPI AddPersonalTrustDBPages(PVOID pvReserved, DWORD dwFlags, PVOID pvParam)
{
    TRACE("(%p %#lx %p)\n", pvReserved, dwFlags, pvParam);

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/***********************************************************************
 *		CatalogCompactHashDatabase (WINTRUST.@)
 */
BOOL WINAPI CatalogCompactHashDatabase(PVOID pvParam1, PVOID pvParam2, PVOID pvParam3, DWORD dwParam4)
{
    TRACE("(%p %p %p %#lx)\n", pvParam1, pvParam2, pvParam3, dwParam4);

    return TRUE;
}

/***********************************************************************
 *		TrustFindIssuerCertificate (WINTRUST.@)
 */
PCCERT_CONTEXT WINAPI TrustFindIssuerCertificate(PCCERT_CONTEXT pChildContext, DWORD dwEncoding, DWORD chStores,
                                                 HCERTSTORE *pahStores, FILETIME *psftVerifyAsOf,
                                                 DWORD *pdwConfidence, DWORD *pdwError, DWORD dwFlags)
{
    TRACE("(%p %#lx %lu %p %p %p %p %#lx)\n", pChildContext, dwEncoding, chStores, pahStores, psftVerifyAsOf,
          pdwConfidence, pdwError, dwFlags);

    if (!pChildContext || dwFlags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return WTHelperCertFindIssuerCertificate(pChildContext, chStores, pahStores, psftVerifyAsOf, dwEncoding,
                                             pdwConfidence, pdwError);
}

/***********************************************************************
 *		WTHelperGetFileHash (WINTRUST.@)
 */
LONG WINAPI WTHelperGetFileHash(LPCWSTR pwszFilename, DWORD dwFlags, PVOID pvReserved, BYTE *pbFileHash,
                                DWORD *pcbFileHash, ALG_ID *pHashAlgid)
{
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO file = { sizeof(file) };
    WINTRUST_DATA data = { sizeof(data) };
    SPC_INDIRECT_DATA_CONTENT *indirect = NULL;
    CRYPT_PROVIDER_DATA *prov;
    const char *oid;
    LONG ret;

    TRACE("(%s %#lx %p %p %p %p)\n", debugstr_w(pwszFilename), dwFlags, pvReserved, pbFileHash, pcbFileHash,
          pHashAlgid);

    file.pcwszFilePath = pwszFilename;
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    ret = WinVerifyTrust(INVALID_HANDLE_VALUE, &action, &data);
    prov = WTHelperProvDataFromStateData(data.hWVTStateData);
    if (!ret && prov && prov->pPDSip)
        indirect = (SPC_INDIRECT_DATA_CONTENT *)prov->pPDSip->psIndirectData;

    if (!indirect)
    {
        if (pcbFileHash)
            *pcbFileHash = 0;
        ret = TRUST_E_NOSIGNATURE;
    }
    else
    {
        oid = indirect->DigestAlgorithm.pszObjId;
        if (pHashAlgid)
            *pHashAlgid = (!strcmp(oid, szOID_OIWSEC_sha1) || !strcmp(oid, szOID_RSA_MD5)) ?
                          CertOIDToAlgId(oid) : CALG_OID_INFO_CNG_ONLY;
        if (pbFileHash && *pcbFileHash < indirect->Digest.cbData)
            ret = ERROR_MORE_DATA;
        else if (pbFileHash)
            memcpy(pbFileHash, indirect->Digest.pbData, indirect->Digest.cbData);
        *pcbFileHash = indirect->Digest.cbData;
    }

    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(INVALID_HANDLE_VALUE, &action, &data);
    return ret;
}

/***********************************************************************
 *		WTHelperCertCheckValidSignature (WINTRUST.@)
 */
HRESULT WINAPI WTHelperCertCheckValidSignature(CRYPT_PROVIDER_DATA *pProvData)
{
    static const DWORD steps[] =
    {
        TRUSTERROR_STEP_FINAL_WVTINIT, TRUSTERROR_STEP_FINAL_INITPROV, TRUSTERROR_STEP_FINAL_OBJPROV,
        TRUSTERROR_STEP_FINAL_SIGPROV, TRUSTERROR_STEP_FINAL_CERTPROV, TRUSTERROR_STEP_FINAL_CERTCHKPROV,
        TRUSTERROR_STEP_FINAL_POLICYPROV,
    };
    DWORD i;

    TRACE("(%p)\n", pProvData);

    for (i = 0; i < ARRAY_SIZE(steps); i++)
    {
        if (steps[i] < pProvData->cdwTrustStepErrors && pProvData->padwTrustStepErrors[steps[i]])
            return pProvData->padwTrustStepErrors[steps[i]];
    }
    return S_OK;
}
#else
/***********************************************************************
 *		WTHelperCertCheckValidSignature
 */
HRESULT WINAPI WTHelperCertCheckValidSignature(CRYPT_PROVIDER_DATA *pProvData)
{
    FIXME("Stub\n");
    return S_OK;
}
#endif

/***********************************************************************
 *              IsCatalogFile
 */
BOOL WINAPI IsCatalogFile(HANDLE hFile, WCHAR *pwszFileName)
{
    static const GUID catGUID = { 0xDE351A43, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE }};
    GUID guid;

    TRACE("(%p, %s)\n", hFile, debugstr_w(pwszFileName));

    if (!CryptSIPRetrieveSubjectGuid(pwszFileName, hFile, &guid))
        return FALSE;
    return IsEqualGUID(&guid, &catGUID);
}

/***********************************************************************
 *              FindCertsByIssuer
 */
#ifdef __REACTOS__
/* TODO: INFO_NEEDED: the 32-bit FindCertsByIssuer layout; only the 64-bit one was measured */
#define WINTRUST_ALIGN_PTR(x) (((x) + sizeof(DWORD_PTR) - 1) & ~(DWORD_PTR)(sizeof(DWORD_PTR) - 1))

struct wintrust_issuer_chain
{
    PCCERT_CHAIN_CONTEXT chain;
    DWORD cert_count;
    CRYPT_KEY_PROV_INFO *key;
};

static CRYPT_KEY_PROV_INFO *WINTRUST_GetKeyProvInfo(PCCERT_CONTEXT cert)
{
    CRYPT_KEY_PROV_INFO *info;
    DWORD size = 0;

    if (!CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, NULL, &size) || !(info = malloc(size)))
        return NULL;
    if (!CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, info, &size))
    {
        free(info);
        return NULL;
    }
    return info;
}

static DWORD WINTRUST_IssuerChainLength(PCCERT_CHAIN_CONTEXT chain, CERT_NAME_BLOB *issuer)
{
    PCERT_SIMPLE_CHAIN simple = chain->rgpChain[0];
    DWORD i;

    for (i = 0; i < simple->cElement; i++)
    {
        if (CertCompareCertificateName(X509_ASN_ENCODING, &simple->rgpElement[i]->pCertContext->pCertInfo->Issuer,
                                       issuer))
            return i + 1 < simple->cElement ? i + 2 : i + 1;
    }
    return 0;
}

static DWORD_PTR WINTRUST_PackKeyProvInfo(const CRYPT_KEY_PROV_INFO *info, BYTE *base, DWORD_PTR offset)
{
    CRYPT_KEY_PROV_INFO *copy = base ? (CRYPT_KEY_PROV_INFO *)(base + offset) : NULL;
    DWORD_PTR size;
    DWORD i;

    if (copy)
        *copy = *info;
    offset += sizeof(*info);
    if (info->pwszContainerName)
    {
        size = (wcslen(info->pwszContainerName) + 1) * sizeof(WCHAR);
        if (copy)
        {
            memcpy(base + offset, info->pwszContainerName, size);
            copy->pwszContainerName = (WCHAR *)(base + offset);
        }
        offset += size;
    }
    if (info->pwszProvName)
    {
        size = (wcslen(info->pwszProvName) + 1) * sizeof(WCHAR);
        if (copy)
        {
            memcpy(base + offset, info->pwszProvName, size);
            copy->pwszProvName = (WCHAR *)(base + offset);
        }
        offset += size;
    }
    if (info->cProvParam)
    {
        offset = WINTRUST_ALIGN_PTR(offset);
        if (copy)
            copy->rgProvParam = (CRYPT_KEY_PROV_PARAM *)(base + offset);
        offset += info->cProvParam * sizeof(CRYPT_KEY_PROV_PARAM);
        for (i = 0; i < info->cProvParam; i++)
        {
            offset = WINTRUST_ALIGN_PTR(offset);
            if (copy)
            {
                copy->rgProvParam[i] = info->rgProvParam[i];
                copy->rgProvParam[i].pbData = base + offset;
                memcpy(base + offset, info->rgProvParam[i].pbData, info->rgProvParam[i].cbData);
            }
            offset += info->rgProvParam[i].cbData;
        }
    }
    return offset;
}

static DWORD_PTR WINTRUST_PackIssuerChains(const struct wintrust_issuer_chain *found, DWORD count, BYTE *base)
{
    CERT_CHAIN *chains = (CERT_CHAIN *)base;
    DWORD_PTR offset = count * sizeof(CERT_CHAIN), blobs, key;
    PCCERT_CONTEXT cert;
    DWORD i, j;

    for (i = 0; i < count; i++)
    {
        blobs = WINTRUST_ALIGN_PTR(offset);
        key = WINTRUST_ALIGN_PTR(blobs + found[i].cert_count * sizeof(CERT_BLOB));
        offset = WINTRUST_PackKeyProvInfo(found[i].key, base, key);
        if (base)
        {
            chains[i].cCerts = found[i].cert_count;
            chains[i].certs = (CERT_BLOB *)(base + blobs);
            chains[i].keyLocatorInfo = *(CRYPT_KEY_PROV_INFO *)(base + key);
        }
        for (j = 0; j < found[i].cert_count; j++)
        {
            cert = found[i].chain->rgpChain[0]->rgpElement[j]->pCertContext;
            offset = WINTRUST_ALIGN_PTR(offset);
            if (base)
            {
                chains[i].certs[j].cbData = cert->cbCertEncoded;
                chains[i].certs[j].pbData = base + offset;
                memcpy(base + offset, cert->pbCertEncoded, cert->cbCertEncoded);
            }
            offset += cert->cbCertEncoded;
        }
    }
    return WINTRUST_ALIGN_PTR(offset);
}
#endif

HRESULT WINAPI FindCertsByIssuer(PCERT_CHAIN pCertChains, DWORD *pcbCertChains,
 DWORD *pcCertChains, BYTE* pbEncodedIssuerName, DWORD cbEncodedIssuerName,
 LPCWSTR pwszPurpose, DWORD dwKeySpec)
{
#ifdef __REACTOS__
    CERT_NAME_BLOB issuer = { cbEncodedIssuerName, pbEncodedIssuerName };
    CERT_CHAIN_PARA para = { sizeof(para) };
    struct wintrust_issuer_chain *found = NULL, *grown;
    PCCERT_CONTEXT cert = NULL;
    PCCERT_CHAIN_CONTEXT chain;
    CRYPT_KEY_PROV_INFO *key;
    DWORD count = 0, length, i;
    DWORD_PTR size;
    HCERTSTORE store;
    HRESULT ret;

    TRACE("(%p, %p, %p, %p, %ld, %s, %ld)\n", pCertChains, pcbCertChains, pcCertChains, pbEncodedIssuerName,
          cbEncodedIssuerName, debugstr_w(pwszPurpose), dwKeySpec);

    store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG,
                          L"My");
    while (store && (cert = CertEnumCertificatesInStore(store, cert)))
    {
        if (!(key = WINTRUST_GetKeyProvInfo(cert)))
            continue;
        chain = NULL;
        length = 0;
        if (key->dwKeySpec == dwKeySpec && CertGetCertificateChain(NULL, cert, NULL, NULL, &para, 0, NULL, &chain))
            length = WINTRUST_IssuerChainLength(chain, &issuer);
        if (length && (grown = realloc(found, (count + 1) * sizeof(*found))))
        {
            found = grown;
            found[count].chain = chain;
            found[count].cert_count = length;
            found[count].key = key;
            count++;
            continue;
        }
        if (chain)
            CertFreeCertificateChain(chain);
        free(key);
    }

    size = count ? WINTRUST_PackIssuerChains(found, count, NULL) : 0;
    if (!count)
        ret = CRYPT_E_NOT_FOUND;
    else if (pCertChains && *pcbCertChains < size)
        ret = HRESULT_FROM_WIN32(ERROR_BAD_LENGTH);
    else
    {
        if (pCertChains)
            WINTRUST_PackIssuerChains(found, count, (BYTE *)pCertChains);
        ret = S_OK;
    }
    *pcbCertChains = (DWORD)size;
    *pcCertChains = count;

    for (i = 0; i < count; i++)
    {
        CertFreeCertificateChain(found[i].chain);
        free(found[i].key);
    }
    free(found);
    if (store)
        CertCloseStore(store, 0);
    return ret;
#else
    FIXME("(%p, %p, %p, %p, %ld, %s, %ld): stub\n", pCertChains, pcbCertChains,
     pcCertChains, pbEncodedIssuerName, cbEncodedIssuerName,
     debugstr_w(pwszPurpose), dwKeySpec);
    return E_FAIL;
#endif
}

/***********************************************************************
 *              WTGetSignatureInfo (WINTRUST.@)
 */
HRESULT WINAPI WTGetSignatureInfo(const WCHAR *path, HANDLE file, DWORD flags,
                                  void *info, void *state, void *unknown)
{
    FIXME("(%s %p %#lx %p %p %p): stub\n", debugstr_w(path), file, flags, info, state, unknown);
    return E_NOTIMPL;
}
