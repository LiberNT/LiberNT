/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test JScript number to string and string to number conversions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <ole2.h>
#include <activscp.h>

static const CLSID CLSID_JScriptTest = {0xf414c260,0x6ac0,0x11cf,{0xb6,0xd1,0x00,0xaa,0x00,0xbb,0xbb,0x58}};

static HRESULT WINAPI Site_QueryInterface(IActiveScriptSite *iface, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IActiveScriptSite))
    {
        *ppv = iface;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI Site_AddRef(IActiveScriptSite *iface) { return 2; }
static ULONG WINAPI Site_Release(IActiveScriptSite *iface) { return 1; }
static HRESULT WINAPI Site_GetLCID(IActiveScriptSite *iface, LCID *plcid) { return E_NOTIMPL; }
static HRESULT WINAPI Site_GetItemInfo(IActiveScriptSite *iface, LPCOLESTR name, DWORD mask, IUnknown **unk, ITypeInfo **ti) { return TYPE_E_ELEMENTNOTFOUND; }
static HRESULT WINAPI Site_GetDocVersionString(IActiveScriptSite *iface, BSTR *version) { return E_NOTIMPL; }
static HRESULT WINAPI Site_OnScriptTerminate(IActiveScriptSite *iface, const VARIANT *result, const EXCEPINFO *ei) { return S_OK; }
static HRESULT WINAPI Site_OnStateChange(IActiveScriptSite *iface, SCRIPTSTATE state) { return S_OK; }
static HRESULT WINAPI Site_OnScriptError(IActiveScriptSite *iface, IActiveScriptError *error) { return S_OK; }
static HRESULT WINAPI Site_OnEnterScript(IActiveScriptSite *iface) { return S_OK; }
static HRESULT WINAPI Site_OnLeaveScript(IActiveScriptSite *iface) { return S_OK; }

static IActiveScriptSiteVtbl SiteVtbl =
{
    Site_QueryInterface, Site_AddRef, Site_Release, Site_GetLCID, Site_GetItemInfo, Site_GetDocVersionString,
    Site_OnScriptTerminate, Site_OnStateChange, Site_OnScriptError, Site_OnEnterScript, Site_OnLeaveScript
};

static IActiveScriptSite Site = { &SiteVtbl };

static const struct
{
    const char *Expression;
    const char *Expected;
    const char *Alternate;
} Tests[] =
{
    { "0.1 + ''", "0.1", NULL },
    { "0.5 + ''", "0.5", NULL },
    { "(1/3) + ''", "0.3333333333333333", NULL },
    { "(2/3) + ''", "0.6666666666666666", NULL },
    { "(100/3) + ''", "33.333333333333336", NULL },
    { "(0.1 + 0.2) + ''", "0.30000000000000004", NULL },
    { "1e21 + ''", "1e+21", NULL },
    { "1e20 + ''", "100000000000000000000", NULL },
    { "123456789012345680000 + ''", "123456789012345680000", NULL },
    { "1.5e300 + ''", "1.5e+300", NULL },
    { "5e-324 + ''", "5e-324", NULL },
    { "1.7976931348623157e308 + ''", "1.7976931348623157e+308", NULL },
    { "1e-6 + ''", "0.000001", NULL },
    { "1e-7 + ''", "1e-7", NULL },
    { "1.5e-7 + ''", "1.5e-7", NULL },
    { "0.000001234 + ''", "0.000001234", NULL },
    { "-0.1 + ''", "-0.1", NULL },
    { "-1e21 + ''", "-1e+21", NULL },
    { "-1.5e-10 + ''", "-1.5e-10", NULL },
    { "2147483648 + ''", "2147483648", NULL },
    { "-2147483649 + ''", "-2147483649", NULL },
    { "4294967296.5 + ''", "4294967296.5", NULL },
    { "9007199254740993 + ''", "9007199254740992", NULL },
    { "(1e15 + 0.3) + ''", "1000000000000000.2", NULL },
    { "3.14159 + ''", "3.14159", NULL },
    { "1.0000000000000002 + ''", "1.0000000000000002", NULL },
    { "123e-20 + ''", "1.23e-18", NULL },
    { "Number.MAX_VALUE + ''", "1.7976931348623157e+308", NULL },
    { "Number.MIN_VALUE + ''", "5e-324", NULL },
    { "String(1e100)", "1e+100", NULL },
    { "[0.1, 1e21, -0, 1.5].join()", "0.1,1e+21,0,1.5", NULL },
    { "(25).toString() + (2.5e25).toString(10)", "252.5e+25", NULL },
    { "(-0) + ''", "0", NULL },
    { "1e16 + ''", "10000000000000000", NULL },
    { "12345678901234567 + ''", "12345678901234568", NULL },
    { "0.000001 + ''", "0.000001", NULL },
    { "0.0000012345678901234567 + ''", "0.0000012345678901234567", NULL },
    { "4.35 + ''", "4.35", NULL },
    { "0.3 * 3 + ''", "0.8999999999999999", NULL },
    { "Math.PI + ''", "3.141592653589793", NULL },
    { "Math.E * 1e10 + ''", "27182818284.59045", NULL },
    { "(1e21 - 65537) + ''", "999999999999999900000", NULL },
    { "(-1e-6) + ''", "-0.000001", NULL },
    { "'5e-324' * 1 + ''", "5e-324", NULL },
    { "(9007199254740992) + ''", "9007199254740992", NULL },
    { "(9007199254740994) + ''", "9007199254740994", NULL },
    { "1e23 + ''", "1e+23", NULL },
    { "(Math.pow(2, 63)) + ''", "9223372036854776000", NULL },
    { "0.125 + ''", "0.125", NULL },
    { "2.5 + ''", "2.5", NULL },
    { "(1e21 + 1e6) + ''", "1.000000000000001e+21", NULL },
    { "1.7e12 + ''", "1700000000000", NULL },
    { "(1700000000000.5) + ''", "1700000000000.5", NULL },
    { "(0.1).toString()", "0.1", NULL },
    { "Number.NaN + '|' + Number.NEGATIVE_INFINITY + '|' + Number.POSITIVE_INFINITY", "NaN|-Infinity|Infinity", NULL },
    { "(delete Number.MAX_VALUE) + '|' + (Number.MAX_VALUE = 1, Number.MAX_VALUE)", "false|1.7976931348623157e+308", NULL },
    { "(function () { var s = ''; for (var k in Number) s += k; return s + '!'; })()", "!", NULL },
    { "Number.prototype.MAX_VALUE + ''", "undefined", NULL },
    { "2.2250738585072014e-308 + ''", "2.2250738585072014e-308", NULL },
    { "2.225073858507201e-308 + ''", "2.225073858507201e-308", NULL },
    { "4.4501477170144023e-308 + ''", "4.4501477170144023e-308", NULL },
    { "123456789012345678901234567890 + ''", "1.2345678901234567e+29", "1.2345678901234568e+29" },
    { "0.1e1 + '|' + .5e-1 + '|' + 5.e2", "1|0.05|500", NULL },
    { "1.0e-400 + '|' + 1e400", "0|Infinity", NULL },
    { "('0.1' * 1 === 0.1) + '|' + ('0.3' * 1) + '|' + ('1.5e3' * 1) + '|' + ('.5' * 1) + '|' + ('5.' * 1)", "true|0.3|1500|0.5|5", NULL },
    { "('.' * 1) + '|' + ('1e' * 1) + '|' + ('  12.5e-1  ' * 1) + '|' + ('+.5e+1' * 1) + '|' + ('-.e1' * 1)", "NaN|NaN|1.25|5|NaN", NULL },
    { "('123456789012345678901234567890' * 1) + '|' + ('1e400' * 1) + '|' + ('-0.0000001' * 1) + '|' + ('1.' * 1)", "1.2345678901234567e+29|Infinity|-1e-7|1", "1.2345678901234568e+29|Infinity|-1e-7|1" },
    { "Number('2.2250738585072014e-308') + '|' + Number('') + '|' + Number('  ') + '|' + Number('0x1F') + '|' + Number('1_000')", "2.2250738585072014e-308|0|0|31|NaN", NULL },
    { "('0.30000000000000004' * 1 === 0.1 + 0.2) + '|' + ('9007199254740993' * 1) + '|' + ('1.7976931348623159e308' * 1)", "true|9007199254740992|Infinity", NULL },
    { "('4.35' * 100) + '|' + (0.000123456789e-200) + '|' + ('1E-7' * 1) + '|' + Number('Infinity') + '|' + Number('-Infinityx')", "434.99999999999994|1.23456789e-204|1e-7|Infinity|NaN", NULL },
};

static void EvaluateExpression(IActiveScriptParse *Parse, const char *Expression, const char *Expected, const char *Alternate)
{
    WCHAR Code[512], ExpectedW[256], AlternateW[256];
    EXCEPINFO ExcepInfo;
    VARIANT Result;
    HRESULT hr;

    MultiByteToWideChar(CP_ACP, 0, Expression, -1, Code, _countof(Code));
    MultiByteToWideChar(CP_ACP, 0, Expected, -1, ExpectedW, _countof(ExpectedW));
    MultiByteToWideChar(CP_ACP, 0, Alternate ? Alternate : Expected, -1, AlternateW, _countof(AlternateW));
    VariantInit(&Result);
    memset(&ExcepInfo, 0, sizeof(ExcepInfo));
    hr = Parse->lpVtbl->ParseScriptText(Parse, Code, NULL, NULL, NULL, 0, 0, SCRIPTTEXT_ISEXPRESSION, &Result, &ExcepInfo);
    ok(hr == S_OK, "%s: hr 0x%08lx\n", Expression, hr);
    ok(V_VT(&Result) == VT_BSTR, "%s: vt %u\n", Expression, V_VT(&Result));
    if (V_VT(&Result) == VT_BSTR)
    {
        ok(!wcscmp(V_BSTR(&Result), ExpectedW) || !wcscmp(V_BSTR(&Result), AlternateW),
           "%s: got %ls, expected %s\n", Expression, V_BSTR(&Result), Expected);
    }
    VariantClear(&Result);
    SysFreeString(ExcepInfo.bstrSource);
    SysFreeString(ExcepInfo.bstrDescription);
    SysFreeString(ExcepInfo.bstrHelpFile);
}

START_TEST(NumberConversion)
{
    IActiveScriptParse *Parse;
    IActiveScript *Script;
    HRESULT hr;
    ULONG i;

    hr = CoInitialize(NULL);
    ok(hr == S_OK, "CoInitialize returned 0x%08lx\n", hr);
    hr = CoCreateInstance(&CLSID_JScriptTest, NULL, CLSCTX_INPROC_SERVER, &IID_IActiveScript, (void **)&Script);
    ok(hr == S_OK, "CoCreateInstance returned 0x%08lx\n", hr);
    if (FAILED(hr))
    {
        CoUninitialize();
        return;
    }
    hr = IActiveScript_QueryInterface(Script, &IID_IActiveScriptParse, (void **)&Parse);
    ok(hr == S_OK, "QueryInterface returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(Parse->lpVtbl->InitNew(Parse) == S_OK, "InitNew failed\n");
        ok(IActiveScript_SetScriptSite(Script, &Site) == S_OK, "SetScriptSite failed\n");
        ok(IActiveScript_SetScriptState(Script, SCRIPTSTATE_STARTED) == S_OK, "SetScriptState failed\n");
        for (i = 0; i < _countof(Tests); i++)
            EvaluateExpression(Parse, Tests[i].Expression, Tests[i].Expected, Tests[i].Alternate);
        IActiveScript_Close(Script);
        Parse->lpVtbl->Release(Parse);
    }
    IActiveScript_Release(Script);
    CoUninitialize();
}
