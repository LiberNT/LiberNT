/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test how WinHttpSendRequest reports a rejected server certificate
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winhttp.h>

static HINTERNET (WINAPI *pWinHttpOpen)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
static HINTERNET (WINAPI *pWinHttpConnect)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
static HINTERNET (WINAPI *pWinHttpOpenRequest)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
static BOOL (WINAPI *pWinHttpSetTimeouts)(HINTERNET, int, int, int, int);
static BOOL (WINAPI *pWinHttpSetOption)(HINTERNET, DWORD, LPVOID, DWORD);
static WINHTTP_STATUS_CALLBACK (WINAPI *pWinHttpSetStatusCallback)(HINTERNET, WINHTTP_STATUS_CALLBACK, DWORD, DWORD_PTR);
static BOOL (WINAPI *pWinHttpSendRequest)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
static BOOL (WINAPI *pWinHttpReceiveResponse)(HINTERNET, LPVOID);
static BOOL (WINAPI *pWinHttpQueryHeaders)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
static BOOL (WINAPI *pWinHttpCloseHandle)(HINTERNET);

static DWORD SecureFailureCalls;
static DWORD SecureFailureFlags;

static
VOID
CALLBACK
StatusCallback(
    HINTERNET Handle,
    DWORD_PTR Context,
    DWORD Status,
    PVOID Information,
    DWORD InformationLength)
{
    if (Status != WINHTTP_CALLBACK_STATUS_SECURE_FAILURE)
        return;
    ok(InformationLength == sizeof(DWORD), "SECURE_FAILURE length %lu\n", InformationLength);
    SecureFailureCalls++;
    SecureFailureFlags |= *(PDWORD)Information;
}

static
BOOL
SendSecure(
    PCWSTR Host,
    DWORD IgnoreFlags,
    PDWORD Error,
    PDWORD StatusCode)
{
    HINTERNET Session, Connect, Request;
    DWORD Size = sizeof(*StatusCode);
    BOOL Ret = FALSE;

    *Error = 0;
    *StatusCode = 0;
    SecureFailureCalls = 0;
    SecureFailureFlags = 0;

    Session = pWinHttpOpen(L"LiberNT apitest", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    ok(Session != NULL, "WinHttpOpen failed with %lu\n", GetLastError());
    if (!Session)
        return FALSE;
    pWinHttpSetTimeouts(Session, 15000, 15000, 15000, 15000);
    Connect = pWinHttpConnect(Session, Host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    ok(Connect != NULL, "WinHttpConnect failed with %lu\n", GetLastError());
    Request = Connect ? pWinHttpOpenRequest(Connect, L"GET", L"/", NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    ok(Request != NULL, "WinHttpOpenRequest failed with %lu\n", GetLastError());
    if (Request)
    {
        if (IgnoreFlags)
            ok(pWinHttpSetOption(Request, WINHTTP_OPTION_SECURITY_FLAGS, &IgnoreFlags, sizeof(IgnoreFlags)),
               "WinHttpSetOption failed with %lu\n", GetLastError());
        pWinHttpSetStatusCallback(Request, StatusCallback, WINHTTP_CALLBACK_STATUS_SECURE_FAILURE, 0);
        Ret = pWinHttpSendRequest(Request, NULL, 0, NULL, 0, 0, 0) && pWinHttpReceiveResponse(Request, NULL);
        if (!Ret)
            *Error = GetLastError();
        else
            pWinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL,
                                StatusCode, &Size, NULL);
        pWinHttpCloseHandle(Request);
    }
    if (Connect)
        pWinHttpCloseHandle(Connect);
    pWinHttpCloseHandle(Session);
    return Ret;
}

static
VOID
TestRejected(
    PCWSTR Host,
    DWORD ExpectedFlag,
    DWORD IgnoreFlag)
{
    DWORD Error, StatusCode;

    ok(!SendSecure(Host, 0, &Error, &StatusCode), "%ls: request succeeded\n", Host);
    ok(Error == ERROR_WINHTTP_SECURE_FAILURE, "%ls: error %lu\n", Host, Error);
    ok(SecureFailureCalls == 1, "%ls: %lu SECURE_FAILURE callbacks\n", Host, SecureFailureCalls);
    ok(SecureFailureFlags == ExpectedFlag, "%ls: SECURE_FAILURE flags %#lx\n", Host, SecureFailureFlags);

    if (!IgnoreFlag)
        return;
    ok(SendSecure(Host, IgnoreFlag, &Error, &StatusCode), "%ls: ignoring %#lx failed with %lu\n",
       Host, IgnoreFlag, Error);
    ok(StatusCode == HTTP_STATUS_OK, "%ls: status %lu\n", Host, StatusCode);
    ok(SecureFailureCalls == 0, "%ls: %lu SECURE_FAILURE callbacks\n", Host, SecureFailureCalls);
}

#define LOAD(name) (p##name = (PVOID)GetProcAddress(Module, #name))

START_TEST(SecureFailure)
{
    DWORD Error, StatusCode;
    HMODULE Module;

    Module = LoadLibraryW(L"winhttp.dll");
    ok(Module != NULL, "LoadLibrary failed with %lu\n", GetLastError());
    if (!Module)
        return;
    if (!LOAD(WinHttpOpen) || !LOAD(WinHttpConnect) || !LOAD(WinHttpOpenRequest) || !LOAD(WinHttpSetTimeouts) ||
        !LOAD(WinHttpSetOption) || !LOAD(WinHttpSetStatusCallback) || !LOAD(WinHttpSendRequest) ||
        !LOAD(WinHttpReceiveResponse) || !LOAD(WinHttpQueryHeaders) || !LOAD(WinHttpCloseHandle))
    {
        ok(FALSE, "winhttp export missing\n");
        FreeLibrary(Module);
        return;
    }

    if (!SendSecure(L"badssl.com", 0, &Error, &StatusCode))
    {
        skip("badssl.com is not reachable (%lu)\n", Error);
        FreeLibrary(Module);
        return;
    }
    ok(StatusCode == HTTP_STATUS_OK, "badssl.com: status %lu\n", StatusCode);
    ok(SecureFailureCalls == 0, "badssl.com: %lu SECURE_FAILURE callbacks\n", SecureFailureCalls);

    TestRejected(L"expired.badssl.com", WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID,
                 SECURITY_FLAG_IGNORE_CERT_DATE_INVALID);
    TestRejected(L"untrusted-root.badssl.com", WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA,
                 SECURITY_FLAG_IGNORE_UNKNOWN_CA);
    TestRejected(L"self-signed.badssl.com", WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA, 0);
    TestRejected(L"wrong.host.badssl.com", WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID,
                 SECURITY_FLAG_IGNORE_CERT_CN_INVALID);
    FreeLibrary(Module);
}
