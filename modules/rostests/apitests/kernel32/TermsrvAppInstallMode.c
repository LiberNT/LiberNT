/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test TermsrvAppInstallMode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_TERMSRV_APP_INSTALL_MODE)(VOID);

START_TEST(TermsrvAppInstallMode)
{
    PFN_TERMSRV_APP_INSTALL_MODE pTermsrvAppInstallMode;

    pTermsrvAppInstallMode = (PFN_TERMSRV_APP_INSTALL_MODE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                          "TermsrvAppInstallMode");
    if (!pTermsrvAppInstallMode)
    {
        skip("TermsrvAppInstallMode is not available\n");
        return;
    }

    SetLastError(0xdeadbeef);
    ok_int(pTermsrvAppInstallMode(), FALSE);
    ok_long(GetLastError(), 0xdeadbeef);
}
