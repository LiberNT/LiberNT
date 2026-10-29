/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A keyboard text service hosted by the test process
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

extern const CLSID CLSID_TestTip;
extern const GUID GUID_TestTipProfile;

struct TEST_TIP_STATE
{
    IUnknown *pTip;
    ITfThreadMgr *pThreadMgr;
    TfClientId tid;
    ULONG cActivate;
    ULONG cDeactivate;
    HRESULT hrAdviseProvider;
    HRESULT hrUnadviseProvider;
    ULONG cTerminated;
    ULONG cEnumDisplayAttributes;
    ULONG cGetDisplayAttribute;
    BOOL fCancelInSession;
};

extern const GUID GUID_TestDisplayAttribute;

typedef HRESULT (WINAPI *FN_TESTTIP_COMPOSE)(LPCWSTR pszText, DWORD dwAction);

#define TESTTIP_START   1
#define TESTTIP_UPDATE  2
#define TESTTIP_END     3
#define TESTTIP_CANCEL  4

extern TEST_TIP_STATE *g_pTip;
extern FN_TESTTIP_COMPOSE g_pfnCompose;

BOOL TestTip_Register(void);
void TestTip_Unregister(void);
BOOL TestTip_Activate(void);
void TestTip_Deactivate(void);
