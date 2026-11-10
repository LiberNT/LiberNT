/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the order and the result of the trust provider steps of WinVerifyTrust
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <stdio.h>

enum
{
    StepsPass,
    StepsObjectFails,
    StepsObjectSetsError,
    StepsInitFails,
    StepsSignatureFails,
};

static char Calls[256];
static int Mode;
static BOOL FinalFails;

static
VOID
LogCall(const char *Name)
{
    if (Calls[0])
        strcat(Calls, " ");
    strcat(Calls, Name);
}

__declspec(dllexport) HRESULT WINAPI TestStepInit(CRYPT_PROVIDER_DATA *Data)
{
    LogCall("init");
    if (Mode != StepsInitFails)
        return S_OK;
    Data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_WVTINIT] = 0x1004;
    return S_FALSE;
}

__declspec(dllexport) HRESULT WINAPI TestStepObject(CRYPT_PROVIDER_DATA *Data)
{
    LogCall("obj");
    if (Mode == StepsObjectFails)
    {
        Data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_OBJPROV] = 0x1001;
        return S_FALSE;
    }
    if (Mode == StepsObjectSetsError)
        Data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_OBJPROV] = 0x1002;
    return S_OK;
}

__declspec(dllexport) HRESULT WINAPI TestStepSignature(CRYPT_PROVIDER_DATA *Data)
{
    LogCall("sig");
    if (Mode != StepsSignatureFails)
        return S_OK;
    Data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_SIGPROV] = 0x1007;
    return S_FALSE;
}

__declspec(dllexport) HRESULT WINAPI TestStepCertificate(CRYPT_PROVIDER_DATA *Data)
{
    LogCall("cert");
    return S_OK;
}

__declspec(dllexport) BOOL WINAPI TestStepCertCheck(CRYPT_PROVIDER_DATA *Data, DWORD Signer, BOOL CounterSigner,
                                                    DWORD CounterSignerIndex)
{
    LogCall("check");
    return TRUE;
}

__declspec(dllexport) HRESULT WINAPI TestStepFinal(CRYPT_PROVIDER_DATA *Data)
{
    LogCall("final");
    if (!FinalFails)
        return S_OK;
    Data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV] = 0x2222;
    return S_FALSE;
}

__declspec(dllexport) HRESULT WINAPI TestStepCleanup(CRYPT_PROVIDER_DATA *Data)
{
    LogCall("cleanup");
    return S_OK;
}

static
VOID
SetEntry(CRYPT_TRUST_REG_ENTRY *Entry, WCHAR *Module, const WCHAR *Function, const WCHAR *DecoratedFunction)
{
    Entry->cbStruct = sizeof(*Entry);
    Entry->pwszDLLName = Function ? Module : NULL;
    Entry->pwszFunctionName = (WCHAR *)(sizeof(PVOID) == sizeof(ULONG) ? DecoratedFunction : Function);
}

static
VOID
CheckMissingProvider(WCHAR *Self, const WCHAR *File)
{
    static const GUID MissingAction = { 0x6b1c1a5e, 0x7777, 0x4000, { 0x80,0x00,0x4c,0x4e,0x54,0x4d,0x49,0x53 } };
    CRYPT_REGISTER_ACTIONID Register = { sizeof(Register) };
    WINTRUST_FILE_INFO FileInfo = { sizeof(FileInfo) };
    WINTRUST_DATA Data = { sizeof(Data) };
    GUID Action = MissingAction;

    FileInfo.pcwszFilePath = File;
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &FileInfo;
    Data.dwStateAction = WTD_STATEACTION_IGNORE;
    SetLastError(0xdeadbeef);
    ok_hex(WinVerifyTrust(INVALID_HANDLE_VALUE, &Action, &Data), TRUST_E_PROVIDER_UNKNOWN);

    SetEntry(&Register.sInitProvider, Self, L"TestStepMissing", L"TestStepMissing@4");
    SetEntry(&Register.sObjectProvider, Self, L"TestStepMissing", L"TestStepMissing@4");
    SetEntry(&Register.sSignatureProvider, Self, L"TestStepMissing", L"TestStepMissing@4");
    SetEntry(&Register.sCertificateProvider, Self, L"TestStepMissing", L"TestStepMissing@4");
    SetEntry(&Register.sCertificatePolicyProvider, Self, L"TestStepMissing", L"TestStepMissing@16");
    SetEntry(&Register.sFinalPolicyProvider, Self, L"TestStepMissing", L"TestStepMissing@4");
    SetEntry(&Register.sTestPolicyProvider, Self, NULL, NULL);
    SetEntry(&Register.sCleanupProvider, Self, L"TestStepMissing", L"TestStepMissing@4");
    ok(WintrustAddActionID(&Action, 0, &Register), "WintrustAddActionID failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok_hex(WinVerifyTrust(INVALID_HANDLE_VALUE, &Action, &Data), TRUST_E_PROVIDER_UNKNOWN);
    ok(WintrustRemoveActionID(&Action), "WintrustRemoveActionID failed with %lu\n", GetLastError());
}

static
VOID
CheckSteps(GUID *Action, const WCHAR *File, int StepMode, BOOL FinalFailure, LONG Expected, const char *ExpectedCalls)
{
    WINTRUST_FILE_INFO FileInfo = { sizeof(FileInfo) };
    WINTRUST_DATA Data = { sizeof(Data) };
    LONG Ret;

    Calls[0] = 0;
    Mode = StepMode;
    FinalFails = FinalFailure;
    FileInfo.pcwszFilePath = File;
    Data.dwUIChoice = WTD_UI_NONE;
    Data.fdwRevocationChecks = WTD_REVOKE_NONE;
    Data.dwUnionChoice = WTD_CHOICE_FILE;
    Data.pFile = &FileInfo;
    Data.dwStateAction = WTD_STATEACTION_IGNORE;
    Ret = WinVerifyTrust(INVALID_HANDLE_VALUE, Action, &Data);
    ok(Ret == Expected, "mode %d final %d: got %#lx, expected %#lx\n", StepMode, FinalFailure, Ret, Expected);
    ok(!strcmp(Calls, ExpectedCalls), "mode %d final %d: calls '%s', expected '%s'\n", StepMode, FinalFailure, Calls,
       ExpectedCalls);
}

START_TEST(TrustSteps)
{
    static const GUID TestAction = { 0x6b1c1a5e, 0x7777, 0x4000, { 0x80,0x00,0x4c,0x4e,0x54,0x53,0x54,0x50 } };
    CRYPT_REGISTER_ACTIONID Register = { sizeof(Register) };
    WCHAR Self[MAX_PATH], Temp[MAX_PATH], File[MAX_PATH];
    GUID Action = TestAction;
    HANDLE Handle;
    DWORD Written;

    GetModuleFileNameW(NULL, Self, RTL_NUMBER_OF(Self));
    GetTempPathW(RTL_NUMBER_OF(Temp), Temp);
    swprintf(File, RTL_NUMBER_OF(File), L"%lslntwtsteps.txt", Temp);
    Handle = CreateFileW(File, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        skip("Cannot create the test file\n");
        return;
    }
    WriteFile(Handle, "x", 1, &Written, NULL);
    CloseHandle(Handle);

    SetEntry(&Register.sInitProvider, Self, L"TestStepInit", L"TestStepInit@4");
    SetEntry(&Register.sObjectProvider, Self, L"TestStepObject", L"TestStepObject@4");
    SetEntry(&Register.sSignatureProvider, Self, L"TestStepSignature", L"TestStepSignature@4");
    SetEntry(&Register.sCertificateProvider, Self, L"TestStepCertificate", L"TestStepCertificate@4");
    SetEntry(&Register.sCertificatePolicyProvider, Self, L"TestStepCertCheck", L"TestStepCertCheck@16");
    SetEntry(&Register.sFinalPolicyProvider, Self, L"TestStepFinal", L"TestStepFinal@4");
    SetEntry(&Register.sTestPolicyProvider, Self, NULL, NULL);
    SetEntry(&Register.sCleanupProvider, Self, L"TestStepCleanup", L"TestStepCleanup@4");
    if (!WintrustAddActionID(&Action, 0, &Register))
    {
        skip("WintrustAddActionID failed with %lu; the test needs administrator rights\n", GetLastError());
        DeleteFileW(File);
        return;
    }

    CheckSteps(&Action, File, StepsPass, FALSE, ERROR_SUCCESS, "init obj sig cert final cleanup");
    CheckSteps(&Action, File, StepsObjectFails, FALSE, ERROR_SUCCESS, "init obj sig cert final cleanup");
    CheckSteps(&Action, File, StepsObjectSetsError, FALSE, ERROR_SUCCESS, "init obj sig cert final cleanup");
    CheckSteps(&Action, File, StepsSignatureFails, FALSE, ERROR_SUCCESS, "init obj sig cert final cleanup");
    CheckSteps(&Action, File, StepsInitFails, FALSE, S_FALSE, "init");
    CheckSteps(&Action, File, StepsPass, TRUE, S_FALSE, "init obj sig cert final cleanup");
    CheckSteps(&Action, File, StepsObjectFails, TRUE, S_FALSE, "init obj sig cert final cleanup");
    CheckSteps(&Action, File, StepsInitFails, TRUE, S_FALSE, "init");

    ok(WintrustRemoveActionID(&Action), "WintrustRemoveActionID failed with %lu\n", GetLastError());
    CheckMissingProvider(Self, File);
    DeleteFileW(File);
}
