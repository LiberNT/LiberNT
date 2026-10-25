/*
 * PropVariant implementation
 *
 * Copyright 2008 James Hawkins for CodeWeavers
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
#include <stdio.h>
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winreg.h"
#include "winuser.h"
#include "shlobj.h"
#include "propvarutil.h"
#include "strsafe.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(propsys);

#define GUID_STR_LEN 38
static HRESULT VARIANT_ValidateType(VARTYPE vt)
{
    VARTYPE vtExtra = vt & (VT_VECTOR | VT_ARRAY | VT_BYREF | VT_RESERVED);

    vt &= VT_TYPEMASK;

    if (!(vtExtra & (VT_VECTOR | VT_RESERVED)))
    {
        if (vt < VT_VOID || vt == VT_RECORD || vt == VT_CLSID)
        {
            if ((vtExtra & (VT_BYREF | VT_ARRAY)) && vt <= VT_NULL)
                return DISP_E_BADVARTYPE;
            if (vt != (VARTYPE)15)
                return S_OK;
        }
    }

    return DISP_E_BADVARTYPE;
}

static HRESULT PROPVAR_ConvertFILETIME(const FILETIME *ft, PROPVARIANT *ppropvarDest, VARTYPE vt)
{
    SYSTEMTIME time;

    FileTimeToSystemTime(ft, &time);

    switch (vt)
    {
        case VT_LPSTR:
            ppropvarDest->pszVal = HeapAlloc(GetProcessHeap(), 0, 64);
            if (!ppropvarDest->pszVal)
                return E_OUTOFMEMORY;

            sprintf( ppropvarDest->pszVal, "%04d/%02d/%02d:%02d:%02d:%02d.%03d",
                      time.wYear, time.wMonth, time.wDay,
                      time.wHour, time.wMinute, time.wSecond,
                      time.wMilliseconds );

            return S_OK;

        default:
            FIXME("Unhandled target type: %d\n", vt);
    }

    return E_FAIL;
}

static HRESULT PROPVAR_ConvertNumber(REFPROPVARIANT pv, int dest_bits,
                                     BOOL dest_signed, LONGLONG *res)
{
    BOOL src_signed;
#ifdef __REACTOS__
    BOOL is_string = FALSE;
#endif

    switch (pv->vt)
    {
    case VT_I1:
        src_signed = TRUE;
        *res = pv->cVal;
        break;
    case VT_UI1:
        src_signed = FALSE;
        *res = pv->bVal;
        break;
    case VT_I2:
        src_signed = TRUE;
        *res = pv->iVal;
        break;
    case VT_UI2:
        src_signed = FALSE;
        *res = pv->uiVal;
        break;
    case VT_I4:
        src_signed = TRUE;
        *res = pv->lVal;
        break;
    case VT_UI4:
        src_signed = FALSE;
        *res = pv->ulVal;
        break;
    case VT_I8:
        src_signed = TRUE;
        *res = pv->hVal.QuadPart;
        break;
    case VT_UI8:
        src_signed = FALSE;
        *res = pv->uhVal.QuadPart;
        break;
    case VT_EMPTY:
#ifdef __REACTOS__
    case VT_NULL:
#endif
        src_signed = FALSE;
        *res = 0;
        break;
    case VT_LPSTR:
    {
        char *end;
        *res = strtoll(pv->pszVal, &end, 0);
        if (pv->pszVal == end)
#ifdef __REACTOS__
            return TYPE_E_TYPEMISMATCH;
        is_string = TRUE;
#else
            return DISP_E_TYPEMISMATCH;
#endif
        src_signed = *res < 0;
        break;
    }
    case VT_LPWSTR:
    case VT_BSTR:
    {
        WCHAR *end;
        *res = wcstol(pv->pwszVal, &end, 0);
        if (pv->pwszVal == end)
#ifdef __REACTOS__
            return TYPE_E_TYPEMISMATCH;
        is_string = TRUE;
#else
            return DISP_E_TYPEMISMATCH;
#endif
        src_signed = *res < 0;
        break;
    }
    case VT_R8:
    {
        src_signed = TRUE;
        *res = pv->dblVal;
        break;
    }
    default:
        FIXME("unhandled vt %d\n", pv->vt);
        return E_NOTIMPL;
    }

#ifdef __REACTOS__
    if (*res < 0 && is_string && !dest_signed)
        return TYPE_E_TYPEMISMATCH;
#endif
    if (*res < 0 && src_signed != dest_signed)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    if (dest_bits < 64)
    {
        if (dest_signed)
        {
            if (*res >= ((LONGLONG)1 << (dest_bits-1)) ||
                *res < ((LONGLONG)-1 << (dest_bits-1)))
                return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }
        else
        {
            if ((ULONGLONG)(*res) >= ((ULONGLONG)1 << dest_bits))
                return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }
    }

    return S_OK;
}

HRESULT WINAPI PropVariantToDouble(REFPROPVARIANT propvarIn, double *ret)
{
    HRESULT hr = S_OK;
    LONGLONG res;

    TRACE("(%p, %p)\n", propvarIn, ret);

    switch (propvarIn->vt)
    {
        case VT_R8:
            *ret = propvarIn->dblVal;
            break;
        case VT_R4:
            *ret = propvarIn->fltVal;
            break;
        default:
            hr = PROPVAR_ConvertNumber(propvarIn, 64, TRUE, &res);
            if (SUCCEEDED(hr)) *ret = (double)res;
    }

    return hr;
}

HRESULT WINAPI PropVariantToInt16(REFPROPVARIANT propvarIn, SHORT *ret)
{
    LONGLONG res;
    HRESULT hr;

    TRACE("%p,%p\n", propvarIn, ret);

    hr = PROPVAR_ConvertNumber(propvarIn, 16, TRUE, &res);
    if (SUCCEEDED(hr)) *ret = (SHORT)res;
    return hr;
}

HRESULT WINAPI PropVariantToInt32(REFPROPVARIANT propvarIn, LONG *ret)
{
    LONGLONG res;
    HRESULT hr;

    TRACE("%p,%p\n", propvarIn, ret);

    hr = PROPVAR_ConvertNumber(propvarIn, 32, TRUE, &res);
    if (SUCCEEDED(hr)) *ret = (LONG)res;
    return hr;
}

HRESULT WINAPI PropVariantToInt64(REFPROPVARIANT propvarIn, LONGLONG *ret)
{
    LONGLONG res;
    HRESULT hr;

    TRACE("%p,%p\n", propvarIn, ret);

    hr = PROPVAR_ConvertNumber(propvarIn, 64, TRUE, &res);
    if (SUCCEEDED(hr)) *ret = res;
    return hr;
}

HRESULT WINAPI PropVariantToUInt16(REFPROPVARIANT propvarIn, USHORT *ret)
{
    LONGLONG res;
    HRESULT hr;

    TRACE("%p,%p\n", propvarIn, ret);

    hr = PROPVAR_ConvertNumber(propvarIn, 16, FALSE, &res);
    if (SUCCEEDED(hr)) *ret = (USHORT)res;
    return hr;
}

HRESULT WINAPI PropVariantToUInt32(REFPROPVARIANT propvarIn, ULONG *ret)
{
    LONGLONG res;
    HRESULT hr;

    TRACE("%p,%p\n", propvarIn, ret);

    hr = PROPVAR_ConvertNumber(propvarIn, 32, FALSE, &res);
    if (SUCCEEDED(hr)) *ret = (ULONG)res;
    return hr;
}

ULONG WINAPI PropVariantToUInt32WithDefault(REFPROPVARIANT propvarIn, ULONG ulDefault)
{
    LONGLONG res;
    HRESULT hr;

    TRACE("%p,%lu\n", propvarIn, ulDefault);

    hr = PROPVAR_ConvertNumber(propvarIn, 32, FALSE, &res);
    if (SUCCEEDED(hr))
        return (ULONG)res;

    return ulDefault;
}

HRESULT WINAPI PropVariantToUInt64(REFPROPVARIANT propvarIn, ULONGLONG *ret)
{
    LONGLONG res;
    HRESULT hr;

    TRACE("%p,%p\n", propvarIn, ret);

    hr = PROPVAR_ConvertNumber(propvarIn, 64, FALSE, &res);
    if (SUCCEEDED(hr)) *ret = (ULONGLONG)res;
    return hr;
}

HRESULT WINAPI PropVariantToBoolean(REFPROPVARIANT propvarIn, BOOL *ret)
{
    LONGLONG res;
    HRESULT hr;

    TRACE("%p,%p\n", propvarIn, ret);

    *ret = FALSE;

    switch (propvarIn->vt)
    {
        case VT_BOOL:
            *ret = propvarIn->boolVal == VARIANT_TRUE;
            return S_OK;

        case VT_LPWSTR:
        case VT_BSTR:
            if (!propvarIn->pwszVal)
                return DISP_E_TYPEMISMATCH;

            if (!lstrcmpiW(propvarIn->pwszVal, L"true") || !lstrcmpW(propvarIn->pwszVal, L"#TRUE#"))
            {
                *ret = TRUE;
                return S_OK;
            }

            if (!lstrcmpiW(propvarIn->pwszVal, L"false") || !lstrcmpW(propvarIn->pwszVal, L"#FALSE#"))
            {
                *ret = FALSE;
                return S_OK;
            }
            break;

         case VT_LPSTR:
            if (!propvarIn->pszVal)
                return DISP_E_TYPEMISMATCH;

            if (!lstrcmpiA(propvarIn->pszVal, "true") || !lstrcmpA(propvarIn->pszVal, "#TRUE#"))
            {
                *ret = TRUE;
                return S_OK;
            }

            if (!lstrcmpiA(propvarIn->pszVal, "false") || !lstrcmpA(propvarIn->pszVal, "#FALSE#"))
            {
                *ret = FALSE;
                return S_OK;
            }
            break;
    }

    hr = PROPVAR_ConvertNumber(propvarIn, 64, TRUE, &res);
    *ret = !!res;
#ifdef __REACTOS__
    if (hr == TYPE_E_TYPEMISMATCH)
        hr = DISP_E_TYPEMISMATCH;
#endif
    return hr;
}

HRESULT WINAPI PropVariantToBSTR(REFPROPVARIANT propvar, BSTR *bstr)
{
    WCHAR *str;
    HRESULT hr;

    TRACE("propvar %p, propvar->vt %#x, bstr %p.\n",
            propvar, propvar ? propvar->vt : 0, bstr);

    if (FAILED(hr = PropVariantToStringAlloc(propvar, &str)))
        return hr;

    *bstr = SysAllocString(str);
    CoTaskMemFree(str);

    if (!*bstr)
        return E_OUTOFMEMORY;

    return S_OK;
}

HRESULT WINAPI PropVariantToBuffer(REFPROPVARIANT propvarIn, void *ret, UINT cb)
{
    HRESULT hr = S_OK;

    TRACE("(%p, %p, %d)\n", propvarIn, ret, cb);

    switch(propvarIn->vt)
    {
        case VT_VECTOR|VT_UI1:
            if(cb > propvarIn->caub.cElems)
                return E_FAIL;
            memcpy(ret, propvarIn->caub.pElems, cb);
            break;
        case VT_ARRAY|VT_UI1:
#ifdef __REACTOS__
        {
            void *data;

            if (SafeArrayGetDim(propvarIn->parray) != 1 || cb > propvarIn->parray->rgsabound[0].cElements)
                return E_FAIL;
            if (!cb)
                break;
            if (FAILED(hr = SafeArrayAccessData(propvarIn->parray, &data)))
                break;
            memcpy(ret, data, cb);
            SafeArrayUnaccessData(propvarIn->parray);
            break;
        }
#else
            FIXME("Unsupported type: VT_ARRAY|VT_UI1\n");
            hr = E_NOTIMPL;
            break;
#endif
        default:
            WARN("Unexpected type: %x\n", propvarIn->vt);
            hr = E_INVALIDARG;
    }

    return hr;
}

HRESULT WINAPI PropVariantToString(REFPROPVARIANT propvarIn, PWSTR ret, UINT cch)
{
    HRESULT hr;
    WCHAR *stringW = NULL;

    TRACE("(%p, %p, %d)\n", propvarIn, ret, cch);

    ret[0] = '\0';

    if(!cch)
        return E_INVALIDARG;

    hr = PropVariantToStringAlloc(propvarIn, &stringW);
    if(SUCCEEDED(hr))
    {
        if(lstrlenW(stringW) >= cch)
            hr = STRSAFE_E_INSUFFICIENT_BUFFER;
        lstrcpynW(ret, stringW, cch);
        CoTaskMemFree(stringW);
    }

    return hr;
}

static HRESULT string_alloc_from_int(const PROPVARIANT *var, WCHAR **ret)
{
    WCHAR buffer[64], *out = buffer + ARRAY_SIZE(buffer) - 1;
    BOOL negative = FALSE;
    ULONG64 value;

    switch (var->vt)
    {
        case VT_I1:
            value = (negative = var->cVal < 0) ? -var->cVal : var->cVal;
            break;
        case VT_I2:
            value = (negative = var->iVal < 0) ? -var->iVal : var->iVal;
            break;
        case VT_I4:
            value = (negative = var->lVal < 0) ? -var->lVal : var->lVal;
            break;
        case VT_I8:
            value = (negative = var->hVal.QuadPart < 0) ? -var->hVal.QuadPart : var->hVal.QuadPart;
            break;
#ifdef __REACTOS__
        case VT_BOOL:
            value = (negative = var->boolVal < 0) ? -var->boolVal : var->boolVal;
            break;
#endif
        case VT_UI1:
            value = var->bVal;
            break;
        case VT_UI2:
            value = var->uiVal;
            break;
        case VT_UI4:
            value = var->ulVal;
            break;
        case VT_UI8:
            value = var->uhVal.QuadPart;
            break;
        default:
            return E_UNEXPECTED;
    }

    *out-- = 0;

    do
    {
        unsigned int next_digit = value % 10;
        *out-- = '0' + next_digit;
        value = value / 10;
    } while (value);

    out++;

    if (negative)
        *--out = '-';

    if (!(*ret = CoTaskMemAlloc((wcslen(out) + 1) * sizeof(*out))))
        return E_OUTOFMEMORY;
    wcscpy(*ret, out);

    return S_OK;
}

#ifdef __REACTOS__
static HRESULT string_alloc_from_double(double value, WCHAR **ret)
{
    WCHAR buffer[64];
    BSTR str;

    if (SUCCEEDED(VarBstrFromR8(value, LOCALE_INVARIANT, 0, &str)))
    {
        if (wcstod(str, NULL) == value && SysStringLen(str) < ARRAY_SIZE(buffer))
            wcscpy(buffer, str);
        else
            swprintf(buffer, ARRAY_SIZE(buffer), L"%.18g", value);
        SysFreeString(str);
    }
    else
        swprintf(buffer, ARRAY_SIZE(buffer), L"%.18g", value);

    if (!(*ret = CoTaskMemAlloc((wcslen(buffer) + 1) * sizeof(WCHAR))))
        return E_OUTOFMEMORY;
    wcscpy(*ret, buffer);

    return S_OK;
}

static HRESULT string_alloc_from_vector(const PROPVARIANT *var, WCHAR **ret)
{
    VARTYPE vt = var->vt & VT_TYPEMASK;
    WCHAR *res, *str, *tmp;
    PROPVARIANT elem;
    SIZE_T len = 1;
    HRESULT hr;
    ULONG i;

    switch (vt)
    {
        case VT_I1:
        case VT_UI1:
        case VT_I2:
        case VT_UI2:
        case VT_I4:
        case VT_UI4:
        case VT_I8:
        case VT_UI8:
        case VT_R4:
        case VT_R8:
        case VT_BOOL:
            break;
        default:
            return TYPE_E_TYPEMISMATCH;
    }

    if (!(res = CoTaskMemAlloc(sizeof(WCHAR))))
        return E_OUTOFMEMORY;
    res[0] = 0;

    for (i = 0; i < var->caub.cElems; i++)
    {
        PropVariantInit(&elem);
        elem.vt = vt;
        switch (vt)
        {
            case VT_I1: elem.cVal = var->cac.pElems[i]; break;
            case VT_UI1: elem.bVal = var->caub.pElems[i]; break;
            case VT_I2: elem.iVal = var->cai.pElems[i]; break;
            case VT_UI2: elem.uiVal = var->caui.pElems[i]; break;
            case VT_I4: elem.lVal = var->cal.pElems[i]; break;
            case VT_UI4: elem.ulVal = var->caul.pElems[i]; break;
            case VT_I8: elem.hVal = var->cah.pElems[i]; break;
            case VT_UI8: elem.uhVal = var->cauh.pElems[i]; break;
            case VT_R4: elem.fltVal = var->caflt.pElems[i]; break;
            case VT_R8: elem.dblVal = var->cadbl.pElems[i]; break;
            case VT_BOOL: elem.boolVal = var->cabool.pElems[i]; break;
        }

        if (FAILED(hr = PropVariantToStringAlloc(&elem, &str)))
        {
            CoTaskMemFree(res);
            return hr;
        }

        len += wcslen(str) + (i ? 2 : 0);
        if (!(tmp = CoTaskMemRealloc(res, len * sizeof(WCHAR))))
        {
            CoTaskMemFree(str);
            CoTaskMemFree(res);
            return E_OUTOFMEMORY;
        }
        res = tmp;
        if (i)
            wcscat(res, L"; ");
        wcscat(res, str);
        CoTaskMemFree(str);
    }

    *ret = res;
    return S_OK;
}
#endif

HRESULT WINAPI PropVariantToStringAlloc(REFPROPVARIANT propvarIn, WCHAR **ret)
{
    WCHAR *res = NULL;
    HRESULT hr = S_OK;

    TRACE("%p,%p semi-stub\n", propvarIn, ret);

    switch(propvarIn->vt)
    {
        case VT_EMPTY:
        case VT_NULL:
            res = CoTaskMemAlloc(1*sizeof(WCHAR));
            res[0] = '\0';
            break;

        case VT_LPSTR:
            if(propvarIn->pszVal)
            {
                DWORD len;

                len = MultiByteToWideChar(CP_ACP, 0, propvarIn->pszVal, -1, NULL, 0);
                res = CoTaskMemAlloc(len*sizeof(WCHAR));
                if(!res)
                    return E_OUTOFMEMORY;

                MultiByteToWideChar(CP_ACP, 0, propvarIn->pszVal, -1, res, len);
            }
            break;

        case VT_LPWSTR:
        case VT_BSTR:
            if (propvarIn->pwszVal)
            {
                DWORD size = (lstrlenW(propvarIn->pwszVal) + 1) * sizeof(WCHAR);
                res = CoTaskMemAlloc(size);
                if(!res) return E_OUTOFMEMORY;
                memcpy(res, propvarIn->pwszVal, size);
            }
            break;

        case VT_CLSID:
            if (propvarIn->puuid)
            {
                if (!(res = CoTaskMemAlloc((GUID_STR_LEN + 1) * sizeof(WCHAR))))
                    return E_OUTOFMEMORY;
                StringFromGUID2(propvarIn->puuid, res, GUID_STR_LEN + 1);
            }
            break;

        case VT_I1:
        case VT_I2:
        case VT_I4:
        case VT_I8:
        case VT_UI1:
        case VT_UI2:
        case VT_UI4:
        case VT_UI8:
#ifdef __REACTOS__
        case VT_BOOL:
#endif
            hr = string_alloc_from_int(propvarIn, &res);
            break;

#ifdef __REACTOS__
        case VT_FILETIME:
        {
            SYSTEMTIME st;

            if (!FileTimeToSystemTime(&propvarIn->filetime, &st))
                return HRESULT_FROM_WIN32(GetLastError());
            if (!(res = CoTaskMemAlloc(24 * sizeof(WCHAR))))
                return E_OUTOFMEMORY;
            swprintf(res, 24, L"%04u/%02u/%02u:%02u:%02u:%02u.%03u", st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            break;
        }

        case VT_R4:
            hr = string_alloc_from_double(propvarIn->fltVal, &res);
            break;

        case VT_R8:
            hr = string_alloc_from_double(propvarIn->dblVal, &res);
            break;

        case VT_DATE:
        {
            SYSTEMTIME st;

            if (!VariantTimeToSystemTime(propvarIn->date, &st))
                return TYPE_E_TYPEMISMATCH;
            if (!(res = CoTaskMemAlloc(24 * sizeof(WCHAR))))
                return E_OUTOFMEMORY;
            swprintf(res, 24, L"%04u/%02u/%02u:%02u:%02u:%02u.%03u", st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            break;
        }

        case VT_VECTOR | VT_LPWSTR:
        {
            SIZE_T len = 1;
            ULONG i;

            for (i = 0; i < propvarIn->calpwstr.cElems; i++)
                len += lstrlenW(propvarIn->calpwstr.pElems[i]) + (i ? 2 : 0);
            if (!(res = CoTaskMemAlloc(len * sizeof(WCHAR))))
                return E_OUTOFMEMORY;
            res[0] = 0;
            for (i = 0; i < propvarIn->calpwstr.cElems; i++)
            {
                if (i)
                    lstrcatW(res, L"; ");
                lstrcatW(res, propvarIn->calpwstr.pElems[i]);
            }
            break;
        }
#endif

        default:
#ifdef __REACTOS__
            if (propvarIn->vt & VT_VECTOR)
            {
                hr = string_alloc_from_vector(propvarIn, &res);
                break;
            }
            FIXME("Unsupported conversion (%d)\n", propvarIn->vt);
            hr = TYPE_E_TYPEMISMATCH;
#else
            FIXME("Unsupported conversion (%d)\n", propvarIn->vt);
            hr = E_FAIL;
#endif
            break;
    }

    *ret = res;

    return hr;
}

PCWSTR WINAPI PropVariantToStringWithDefault(REFPROPVARIANT propvarIn, LPCWSTR pszDefault)
{
    if (propvarIn->vt == VT_BSTR)
    {
        if (propvarIn->bstrVal == NULL)
            return L"";

        return propvarIn->bstrVal;
    }

    if (propvarIn->vt == VT_LPWSTR && propvarIn->pwszVal != NULL)
        return propvarIn->pwszVal;

    return pszDefault;
}

/******************************************************************
 *  VariantToStringWithDefault   (PROPSYS.@)
 */
PCWSTR WINAPI VariantToStringWithDefault(const VARIANT *pvar, const WCHAR *default_value)
{
    TRACE("%s, %s.\n", debugstr_variant(pvar), debugstr_w(default_value));

    if (V_VT(pvar) == (VT_BYREF | VT_VARIANT)) pvar = V_VARIANTREF(pvar);
    if (V_VT(pvar) == (VT_BYREF | VT_BSTR) || V_VT(pvar) == VT_BSTR)
    {
        BSTR ret = V_ISBYREF(pvar) ? *V_BSTRREF(pvar) : V_BSTR(pvar);
        return ret ? ret : L"";
    }

    return default_value;
}

/******************************************************************
 *  VariantToString   (PROPSYS.@)
 */
HRESULT WINAPI VariantToString(REFVARIANT var, PWSTR ret, UINT cch)
{
    WCHAR buffer[64], *str = buffer;

    TRACE("%p, %p, %u.\n", var, ret, cch);

    *ret = 0;

    if (!cch)
        return E_INVALIDARG;

    switch (V_VT(var))
    {
#ifdef __REACTOS__
        case VT_EMPTY:
            return S_OK;
#endif
        case VT_BSTR:
            str = V_BSTR(var);
            break;
        case VT_I4:
            swprintf(buffer, ARRAY_SIZE(buffer), L"%d", V_I4(var));
            break;
        default:
            FIXME("Unsupported type %d.\n", V_VT(var));
            return E_NOTIMPL;
    }

    if (wcslen(str) > cch - 1)
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    wcscpy(ret, str);

    return S_OK;
}

/******************************************************************
 *  PropVariantChangeType   (PROPSYS.@)
 */
#ifdef __REACTOS__
static HRESULT propvar_change_type(PROPVARIANT *ppropvarDest, REFPROPVARIANT propvarSrc,
                                   PROPVAR_CHANGE_FLAGS flags, VARTYPE vt)
#else
HRESULT WINAPI PropVariantChangeType(PROPVARIANT *ppropvarDest, REFPROPVARIANT propvarSrc,
                                     PROPVAR_CHANGE_FLAGS flags, VARTYPE vt)
#endif
{
    HRESULT hr;

    TRACE("(%p, %p, %d, %d, %d)\n", ppropvarDest, propvarSrc, propvarSrc->vt, flags, vt);

    if (vt == propvarSrc->vt)
        return PropVariantCopy(ppropvarDest, propvarSrc);

    if (propvarSrc->vt == VT_FILETIME)
        return PROPVAR_ConvertFILETIME(&propvarSrc->filetime, ppropvarDest, vt);

    switch (vt)
    {
    case VT_I1:
    {
        LONGLONG res;

        hr = PROPVAR_ConvertNumber(propvarSrc, 8, TRUE, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_I1;
            ppropvarDest->cVal = (char)res;
        }
        return hr;
    }

    case VT_UI1:
    {
        LONGLONG res;

        hr = PROPVAR_ConvertNumber(propvarSrc, 8, FALSE, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_UI1;
            ppropvarDest->bVal = (UCHAR)res;
        }
        return hr;
    }

    case VT_I2:
    {
        SHORT res;
        hr = PropVariantToInt16(propvarSrc, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_I2;
            ppropvarDest->iVal = res;
        }
        return hr;
    }
    case VT_UI2:
    {
        USHORT res;
        hr = PropVariantToUInt16(propvarSrc, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_UI2;
            ppropvarDest->uiVal = res;
        }
        return hr;
    }
    case VT_I4:
    {
        LONG res;
        hr = PropVariantToInt32(propvarSrc, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_I4;
            ppropvarDest->lVal = res;
        }
        return hr;
    }
    case VT_UI4:
    {
        ULONG res;
        hr = PropVariantToUInt32(propvarSrc, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_UI4;
            ppropvarDest->ulVal = res;
        }
        return hr;
    }
    case VT_I8:
    {
        LONGLONG res;
        hr = PropVariantToInt64(propvarSrc, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_I8;
            ppropvarDest->hVal.QuadPart = res;
        }
        return hr;
    }
    case VT_UI8:
    {
        ULONGLONG res;
        hr = PropVariantToUInt64(propvarSrc, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_UI8;
            ppropvarDest->uhVal.QuadPart = res;
        }
        return hr;
    }
    case VT_R8:
    {
        double res;
        hr = PropVariantToDouble(propvarSrc, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_R8;
            ppropvarDest->dblVal = res;
        }
        return hr;
    }
    case VT_R4:
    {
        double res;
        hr = PropVariantToDouble(propvarSrc, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_R4;
            ppropvarDest->fltVal = res;
        }
        return hr;
    }
    case VT_BOOL:
    {
        BOOL res;
        hr = PropVariantToBoolean(propvarSrc, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_BOOL;
            ppropvarDest->boolVal = res ? VARIANT_TRUE : VARIANT_FALSE;
        }
        return hr;
    }

    case VT_LPWSTR:
    case VT_BSTR:
    {
        WCHAR *res;
        hr = PropVariantToStringAlloc(propvarSrc, &res);
        if (SUCCEEDED(hr))
        {
            ppropvarDest->vt = VT_LPWSTR;
            ppropvarDest->pwszVal = res;
        }
        return hr;
    }

    case VT_LPSTR:
    {
        WCHAR *resW;
        hr = PropVariantToStringAlloc(propvarSrc, &resW);
        if (SUCCEEDED(hr))
        {
            char *res;
            DWORD len;

            len = WideCharToMultiByte(CP_ACP, 0, resW, -1, NULL, 0, NULL, NULL);
            res = CoTaskMemAlloc(len);
            if (res)
            {
                WideCharToMultiByte(CP_ACP, 0, resW, -1, res, len, NULL, NULL);
                ppropvarDest->vt = VT_LPSTR;
                ppropvarDest->pszVal = res;
            }
            else
                hr = E_OUTOFMEMORY;

            CoTaskMemFree(resW);
        }
        return hr;
    }

    case VT_CLSID:
    {
        GUID guid;
        if (SUCCEEDED((hr = PropVariantToGUID(propvarSrc, &guid))))
            hr = InitPropVariantFromCLSID(&guid, ppropvarDest);
        return hr;
    }

    default:
        FIXME("Unhandled dest type: %d\n", vt);
        return E_FAIL;
    }
}

#ifdef __REACTOS__
HRESULT WINAPI PropVariantChangeType(PROPVARIANT *ppropvarDest, REFPROPVARIANT propvarSrc,
                                     PROPVAR_CHANGE_FLAGS flags, VARTYPE vt)
{
    HRESULT hr = propvar_change_type(ppropvarDest, propvarSrc, flags, vt);

    if (FAILED(hr))
        PropVariantInit(ppropvarDest);

    return hr;
}
#endif
HRESULT WINAPI InitPropVariantFromGUIDAsString(REFGUID guid, PROPVARIANT *ppropvar)
{
    TRACE("(%p %p)\n", guid, ppropvar);

    if(!guid)
        return E_FAIL;

    ppropvar->vt = VT_LPWSTR;
    ppropvar->pwszVal = CoTaskMemAlloc((GUID_STR_LEN + 1) * sizeof(WCHAR));
    if(!ppropvar->pwszVal)
        return E_OUTOFMEMORY;

    StringFromGUID2(guid, ppropvar->pwszVal, GUID_STR_LEN + 1);
    return S_OK;
}

HRESULT WINAPI InitVariantFromGUIDAsString(REFGUID guid, VARIANT *pvar)
{
    TRACE("(%p %p)\n", guid, pvar);

    if(!guid) {
        FIXME("guid == NULL\n");
        return E_FAIL;
    }

    V_VT(pvar) = VT_BSTR;
    V_BSTR(pvar) = SysAllocStringLen(NULL, GUID_STR_LEN);
    if(!V_BSTR(pvar))
        return E_OUTOFMEMORY;

    StringFromGUID2(guid, V_BSTR(pvar), GUID_STR_LEN + 1);
    return S_OK;
}

HRESULT WINAPI InitPropVariantFromBuffer(const VOID *pv, UINT cb, PROPVARIANT *ppropvar)
{
    TRACE("(%p %u %p)\n", pv, cb, ppropvar);

    ppropvar->caub.pElems = CoTaskMemAlloc(cb);
    if(!ppropvar->caub.pElems)
        return E_OUTOFMEMORY;

    ppropvar->vt = VT_VECTOR|VT_UI1;
    ppropvar->caub.cElems = cb;
    memcpy(ppropvar->caub.pElems, pv, cb);
    return S_OK;
}

HRESULT WINAPI InitPropVariantFromCLSID(REFCLSID clsid, PROPVARIANT *ppropvar)
{
    TRACE("(%s %p)\n", debugstr_guid(clsid), ppropvar);

    ppropvar->puuid = CoTaskMemAlloc(sizeof(*ppropvar->puuid));
    if(!ppropvar->puuid)
        return E_OUTOFMEMORY;

    ppropvar->vt = VT_CLSID;
    memcpy(ppropvar->puuid, clsid, sizeof(*ppropvar->puuid));
    return S_OK;
}

#ifdef __REACTOS__
HRESULT WINAPI InitPropVariantFromFileTime(const FILETIME *ft, PROPVARIANT *ppropvar)
{
    TRACE("(%p %p)\n", ft, ppropvar);

    if (!ft || !ppropvar)
        return E_INVALIDARG;

    PropVariantInit(ppropvar);
    ppropvar->vt = VT_FILETIME;
    ppropvar->filetime = *ft;
    return S_OK;
}
#endif

HRESULT WINAPI InitPropVariantFromStringVector(PCWSTR *strs, ULONG count, PROPVARIANT *ppropvar)
{
    unsigned int i;

    TRACE("(%p %lu %p)\n", strs, count, ppropvar);

    ppropvar->calpwstr.pElems = CoTaskMemAlloc(count * sizeof(*ppropvar->calpwstr.pElems));
    if(!ppropvar->calpwstr.pElems)
        return E_OUTOFMEMORY;

    ppropvar->vt = VT_LPWSTR | VT_VECTOR;
    ppropvar->calpwstr.cElems = 0;
    if (count)
        memset(ppropvar->calpwstr.pElems, 0, count * sizeof(*ppropvar->calpwstr.pElems));

    for (i = 0; i < count; ++i)
    {
        if (strs[i])
        {
            if (!(ppropvar->calpwstr.pElems[i] = CoTaskMemAlloc((wcslen(strs[i]) + 1)*sizeof(**strs))))
            {
                PropVariantClear(ppropvar);
                return E_OUTOFMEMORY;
            }
        }
        wcscpy(ppropvar->calpwstr.pElems[i], strs[i]);
        ppropvar->calpwstr.cElems++;
    }

    return S_OK;
}

HRESULT WINAPI InitVariantFromBuffer(const VOID *pv, UINT cb, VARIANT *pvar)
{
    SAFEARRAY *arr;
    void *data;
    HRESULT hres;

    TRACE("(%p %u %p)\n", pv, cb, pvar);

    arr = SafeArrayCreateVector(VT_UI1, 0, cb);
    if(!arr)
        return E_OUTOFMEMORY;

    hres = SafeArrayAccessData(arr, &data);
    if(FAILED(hres)) {
        SafeArrayDestroy(arr);
        return hres;
    }

    memcpy(data, pv, cb);

    hres = SafeArrayUnaccessData(arr);
    if(FAILED(hres)) {
        SafeArrayDestroy(arr);
        return hres;
    }

    V_VT(pvar) = VT_ARRAY|VT_UI1;
    V_ARRAY(pvar) = arr;
    return S_OK;
}

HRESULT WINAPI InitVariantFromFileTime(const FILETIME *ft, VARIANT *var)
{
    SYSTEMTIME st;

    TRACE("%p, %p\n", ft, var);

    VariantInit(var);
    if (!FileTimeToSystemTime(ft, &st))
        return E_INVALIDARG;
    if (!SystemTimeToVariantTime(&st, &V_DATE(var)))
        return E_INVALIDARG;
    V_VT(var) = VT_DATE;
    return S_OK;
}

static inline DWORD PROPVAR_HexToNum(const WCHAR *hex)
{
    DWORD ret;

    if(hex[0]>='0' && hex[0]<='9')
        ret = hex[0]-'0';
    else if(hex[0]>='a' && hex[0]<='f')
        ret = hex[0]-'a'+10;
    else if(hex[0]>='A' && hex[0]<='F')
        ret = hex[0]-'A'+10;
    else
        return -1;

    ret <<= 4;
    if(hex[1]>='0' && hex[1]<='9')
        return ret + hex[1]-'0';
    else if(hex[1]>='a' && hex[1]<='f')
        return ret + hex[1]-'a'+10;
    else if(hex[1]>='A' && hex[1]<='F')
        return ret + hex[1]-'A'+10;
    else
        return -1;
}

static inline HRESULT PROPVAR_WCHARToGUID(const WCHAR *str, int len, GUID *guid)
{
    DWORD i, val=0;
    const WCHAR *p;

    memset(guid, 0, sizeof(GUID));

    if(len!=38 || str[0]!='{' || str[9]!='-' || str[14]!='-'
            || str[19]!='-' || str[24]!='-' || str[37]!='}') {
        WARN("Error parsing %s\n", debugstr_w(str));
        return E_INVALIDARG;
    }

    p = str+1;
    for(i=0; i<4 && val!=-1; i++) {
        val = PROPVAR_HexToNum(p);
        guid->Data1 = (guid->Data1<<8) + val;
        p += 2;
    }
    p++;
    for(i=0; i<2 && val!=-1; i++) {
        val = PROPVAR_HexToNum(p);
        guid->Data2 = (guid->Data2<<8) + val;
        p += 2;
    }
    p++;
    for(i=0; i<2 && val!=-1; i++) {
        val = PROPVAR_HexToNum(p);
        guid->Data3 = (guid->Data3<<8) + val;
        p += 2;
    }
    p++;
    for(i=0; i<8 && val!=-1; i++) {
        if(i == 2)
            p++;

        val = guid->Data4[i] = PROPVAR_HexToNum(p);
        p += 2;
    }

    if(val == -1) {
        WARN("Error parsing %s\n", debugstr_w(str));
        memset(guid, 0, sizeof(GUID));
        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT WINAPI PropVariantToGUID(REFPROPVARIANT ppropvar, GUID *guid)
{
    TRACE("%p %p)\n", ppropvar, guid);

    switch(ppropvar->vt) {
    case VT_BSTR:
        return PROPVAR_WCHARToGUID(ppropvar->bstrVal, SysStringLen(ppropvar->bstrVal), guid);
    case VT_LPWSTR:
        return PROPVAR_WCHARToGUID(ppropvar->pwszVal, lstrlenW(ppropvar->pwszVal), guid);
    case VT_CLSID:
        memcpy(guid, ppropvar->puuid, sizeof(*ppropvar->puuid));
        return S_OK;
#ifdef __REACTOS__
    case VT_EMPTY:
    case VT_NULL:
        memset(guid, 0, sizeof(*guid));
        return S_OK;
    case VT_ARRAY | VT_UI1:
    {
        void *data;
        HRESULT hr;

        if (SafeArrayGetDim(ppropvar->parray) != 1 || ppropvar->parray->rgsabound[0].cElements < sizeof(*guid))
            return E_FAIL;
        if (FAILED(hr = SafeArrayAccessData(ppropvar->parray, &data)))
            return hr;
        memcpy(guid, data, sizeof(*guid));
        SafeArrayUnaccessData(ppropvar->parray);
        return S_OK;
    }
#endif

    default:
        FIXME("unsupported vt: %d\n", ppropvar->vt);
#ifdef __REACTOS__
        return E_INVALIDARG;
#else
        return E_NOTIMPL;
#endif
    }
}

HRESULT WINAPI VariantToGUID(const VARIANT *pvar, GUID *guid)
{
    TRACE("(%p %p)\n", pvar, guid);

    switch(V_VT(pvar)) {
    case VT_BSTR: {
        HRESULT hres = PROPVAR_WCHARToGUID(V_BSTR(pvar), SysStringLen(V_BSTR(pvar)), guid);
        if(hres == E_INVALIDARG)
            return E_FAIL;
        return hres;
    }

    default:
        FIXME("unsupported vt: %d\n", V_VT(pvar));
        return E_NOTIMPL;
    }
}

static BOOL isemptyornull(const PROPVARIANT *propvar)
{
    if (propvar->vt == VT_EMPTY || propvar->vt == VT_NULL)
        return TRUE;
    if ((propvar->vt & VT_ARRAY) == VT_ARRAY)
    {
        int i;
        for (i=0; i<propvar->parray->cDims; i++)
        {
            if (propvar->parray->rgsabound[i].cElements != 0)
                break;
        }
        return i == propvar->parray->cDims;
    }
    if (propvar->vt == VT_CLSID)
        return !propvar->puuid;

    if (propvar->vt & VT_VECTOR)
        return !propvar->caub.cElems;

    /* FIXME: byrefs, errors? */
    return FALSE;
}

INT WINAPI PropVariantCompareEx(REFPROPVARIANT propvar1, REFPROPVARIANT propvar2,
    PROPVAR_COMPARE_UNIT unit, PROPVAR_COMPARE_FLAGS flags)
{
    const PROPVARIANT *propvar2_converted;
    PROPVARIANT propvar2_static;
    unsigned int count;
    HRESULT hr;
    INT res=-1;

    TRACE("%p,%p,%x,%x\n", propvar1, propvar2, unit, flags);

    if (isemptyornull(propvar1))
    {
        if (isemptyornull(propvar2))
            return 0;
        return (flags & PVCF_TREATEMPTYASGREATERTHAN) ? 1 : -1;
    }

    if (isemptyornull(propvar2))
        return (flags & PVCF_TREATEMPTYASGREATERTHAN) ? -1 : 1;

    if (propvar1->vt != propvar2->vt)
    {
        hr = PropVariantChangeType(&propvar2_static, propvar2, 0, propvar1->vt);

        if (FAILED(hr))
#ifdef __REACTOS__
            return (INT)propvar1->vt - (INT)propvar2->vt;
#else
            return -1;
#endif

        propvar2_converted = &propvar2_static;
    }
    else
        propvar2_converted = propvar2;

#define CMP_NUM_VALUE(var) do { \
    if (propvar1->var > propvar2_converted->var) \
        res = 1; \
    else if (propvar1->var < propvar2_converted->var) \
        res = -1; \
    else \
        res = 0; \
    } while (0)

    switch (propvar1->vt)
    {
    case VT_I1:
        CMP_NUM_VALUE(cVal);
        break;
    case VT_UI1:
        CMP_NUM_VALUE(bVal);
        break;
    case VT_I2:
        CMP_NUM_VALUE(iVal);
        break;
    case VT_UI2:
        CMP_NUM_VALUE(uiVal);
        break;
    case VT_I4:
        CMP_NUM_VALUE(lVal);
        break;
    case VT_UI4:
        CMP_NUM_VALUE(ulVal);
        break;
    case VT_I8:
        CMP_NUM_VALUE(hVal.QuadPart);
        break;
    case VT_UI8:
        CMP_NUM_VALUE(uhVal.QuadPart);
        break;
#ifdef __REACTOS__
    case VT_BOOL:
        CMP_NUM_VALUE(boolVal);
        break;
    case VT_FILETIME:
        res = CompareFileTime(&propvar1->filetime, &propvar2_converted->filetime);
        break;
#endif
    case VT_R4:
        CMP_NUM_VALUE(fltVal);
        break;
    case VT_R8:
        CMP_NUM_VALUE(dblVal);
        break;
    case VT_BSTR:
    case VT_LPWSTR:
        /* FIXME: Use other string flags. */
        if (flags & (PVCF_USESTRCMPI | PVCF_USESTRCMPIC))
            res = lstrcmpiW(propvar1->bstrVal, propvar2_converted->bstrVal);
        else
            res = lstrcmpW(propvar1->bstrVal, propvar2_converted->bstrVal);
        break;
    case VT_LPSTR:
        /* FIXME: Use other string flags. */
        if (flags & (PVCF_USESTRCMPI | PVCF_USESTRCMPIC))
            res = lstrcmpiA(propvar1->pszVal, propvar2_converted->pszVal);
        else
            res = lstrcmpA(propvar1->pszVal, propvar2_converted->pszVal);
        break;
    case VT_CLSID:
        res = memcmp(propvar1->puuid, propvar2->puuid, sizeof(*propvar1->puuid));
        if (res) res = res > 0 ? 1 : -1;
        break;
    case VT_VECTOR | VT_UI1:
        count = min(propvar1->caub.cElems, propvar2->caub.cElems);
        res = count ? memcmp(propvar1->caub.pElems, propvar2->caub.pElems, sizeof(*propvar1->caub.pElems) * count) : 0;
        if (res) res = res > 0 ? 1 : -1;
        if (!res && propvar1->caub.cElems != propvar2->caub.cElems)
            res = propvar1->caub.cElems > propvar2->caub.cElems ? 1 : -1;
        break;
    default:
        FIXME("vartype %#x not handled\n", propvar1->vt);
        res = -1;
        break;
    }

    if (propvar2_converted == &propvar2_static)
        PropVariantClear(&propvar2_static);

    return res;
}

HRESULT WINAPI PropVariantToVariant(const PROPVARIANT *propvar, VARIANT *var)
{
    HRESULT hr = S_OK;

    TRACE("propvar %p, var %p, propvar->vt %#x.\n", propvar, var, propvar ? propvar->vt : 0);

    if (!var || !propvar)
        return E_INVALIDARG;

    VariantInit(var);
    var->vt = propvar->vt;

    switch (propvar->vt)
    {
        case VT_EMPTY:
        case VT_NULL:
            break;
        case VT_I1:
            V_I1(var) = propvar->cVal;
            break;
        case VT_I2:
            V_I2(var) = propvar->iVal;
            break;
        case VT_I4:
            V_I4(var) = propvar->lVal;
            break;
        case VT_I8:
            V_I8(var) = propvar->hVal.QuadPart;
            break;
        case VT_UI1:
            V_UI1(var) = propvar->bVal;
            break;
        case VT_UI2:
            V_UI2(var) = propvar->uiVal;
            break;
        case VT_UI4:
            V_UI4(var) = propvar->ulVal;
            break;
        case VT_UI8:
            V_UI8(var) = propvar->uhVal.QuadPart;
            break;
        case VT_BOOL:
            V_BOOL(var) = propvar->boolVal;
            break;
        case VT_R4:
            V_R4(var) = propvar->fltVal;
            break;
        case VT_R8:
            V_R8(var) = propvar->dblVal;
            break;
#ifdef __REACTOS__
        case VT_CLSID:
            var->vt = VT_BSTR;
            if (!(V_BSTR(var) = SysAllocStringLen(NULL, GUID_STR_LEN)))
                return E_OUTOFMEMORY;
            hr = StringFromGUID2(propvar->puuid, V_BSTR(var), GUID_STR_LEN + 1);
            break;
        case VT_LPSTR:
        case VT_LPWSTR:
        case VT_BSTR:
#else
        case VT_LPSTR:
        case VT_LPWSTR:
        case VT_BSTR:
        case VT_CLSID:
#endif
            var->vt = VT_BSTR;
            hr = PropVariantToBSTR(propvar, &V_BSTR(var));
            break;
        default:
            FIXME("Unsupported type %d.\n", propvar->vt);
#ifdef __REACTOS__
            var->vt = VT_EMPTY;
            return (propvar->vt & VT_RESERVED) ? E_OUTOFMEMORY : E_INVALIDARG;
#else
            return E_INVALIDARG;
#endif
    }

    return hr;
}

HRESULT WINAPI VariantToPropVariant(const VARIANT *var, PROPVARIANT *propvar)
{
    HRESULT hr;

    TRACE("var %p, propvar %p.\n", debugstr_variant(var), propvar);

    if (!var || !propvar)
        return E_INVALIDARG;

#ifdef __REACTOS__
    if (var->vt == VT_ILLEGAL)
        return TYPE_E_TYPEMISMATCH;
    if (var->vt == VT_CLSID)
        return DISP_E_BADVARTYPE;
#endif

    if (FAILED(hr = VARIANT_ValidateType(var->vt)))
        return hr;

    PropVariantInit(propvar);
    propvar->vt = var->vt;

    switch (var->vt)
    {
        case VT_EMPTY:
        case VT_NULL:
            break;
        case VT_I1:
            propvar->cVal = V_I1(var);
            break;
        case VT_I2:
            propvar->iVal = V_I2(var);
            break;
        case VT_I4:
            propvar->lVal = V_I4(var);
            break;
        case VT_I8:
            propvar->hVal.QuadPart = V_I8(var);
            break;
        case VT_UI1:
            propvar->bVal = V_UI1(var);
            break;
        case VT_UI2:
            propvar->uiVal = V_UI2(var);
            break;
        case VT_UI4:
            propvar->ulVal = V_UI4(var);
            break;
        case VT_UI8:
            propvar->uhVal.QuadPart = V_UI8(var);
            break;
        case VT_BOOL:
            propvar->boolVal = V_BOOL(var);
            break;
        case VT_R4:
            propvar->fltVal = V_R4(var);
            break;
        case VT_R8:
            propvar->dblVal = V_R8(var);
            break;
        case VT_BSTR:
            propvar->bstrVal = SysAllocString(V_BSTR(var));
            break;
        default:
            FIXME("Unsupported type %d.\n", var->vt);
            return E_INVALIDARG;
    }

    return S_OK;
}

INT WINAPI VariantCompare(REFVARIANT var1, REFVARIANT var2)
{
    PROPVARIANT propvar1, propvar2;
    INT res = 0;

    TRACE("%s, %s.\n", debugstr_variant(var1), debugstr_variant(var2));

    PropVariantInit(&propvar1);
    PropVariantInit(&propvar2);

    if (SUCCEEDED(VariantToPropVariant(var1, &propvar1)) &&
        SUCCEEDED(VariantToPropVariant(var2, &propvar2)))
    {
        res = PropVariantCompareEx(&propvar1, &propvar2, PVCU_DEFAULT, 0);
    }

    PropVariantClear(&propvar1);
    PropVariantClear(&propvar2);

    return res;
}

HRESULT WINAPI PropVariantGetStringElem(const PROPVARIANT *propvar, ULONG idx, WCHAR **ret)
{
    const WCHAR *wstr;

    TRACE("propvar %p, idx %lu, ret %p, propvar->vt %#x.\n", propvar, idx, ret, propvar ? propvar->vt : 0);

    *ret = 0;
    switch (propvar->vt)
    {
    case VT_BSTR:
        if (idx != 0) return E_INVALIDARG;
        wstr = propvar->bstrVal;
        break;
    case VT_LPWSTR:
        if (idx != 0) return E_INVALIDARG;
        wstr = propvar->pwszVal;
        break;
    case VT_BSTR | VT_VECTOR:
        if (idx >= propvar->cabstr.cElems) return E_INVALIDARG;
        wstr = propvar->cabstr.pElems[idx];
        break;
    case VT_LPWSTR | VT_VECTOR:
        if (idx >= propvar->calpwstr.cElems) return E_INVALIDARG;
        wstr = propvar->calpwstr.pElems[idx];
        break;
    case VT_BSTR | VT_ARRAY:
    {
        LONG l = idx;
        HRESULT hr;

        if (SafeArrayGetDim(propvar->parray) != 1) return E_INVALIDARG;
        /* bug in native implementation: ignore lower-bound when checking bounds */
        if (idx >= propvar->parray->rgsabound[0].cElements) return E_INVALIDARG;
        hr = SafeArrayGetElement(propvar->parray, &l, &wstr);
        if (FAILED(hr)) return hr;
        break;
    }
    default:
        return E_INVALIDARG;
    }

    if (!wstr) wstr = L"";
    *ret = CoTaskMemAlloc((wcslen(wstr) + 1) * sizeof(WCHAR));
    if (!*ret)
        return E_OUTOFMEMORY;
    wcscpy(*ret, wstr);
    return S_OK;
}

HRESULT WINAPI PropVariantToFileTime(REFPROPVARIANT propvar, PSTIME_FLAGS flags, FILETIME *timestamp)
{
    TRACE("%p, %#x, %p\n", propvar, flags, timestamp);

    switch (propvar->vt)
    {
    case VT_EMPTY:
        return E_INVALIDARG;
#ifdef __REACTOS__
    case VT_FILETIME:
        if (flags & PSTF_LOCAL)
            return FileTimeToLocalFileTime(&propvar->filetime, timestamp) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        *timestamp = propvar->filetime;
        return S_OK;
#endif
    default:
        FIXME("Unimplemented for type %d.\n", propvar->vt);
        return E_NOTIMPL;
    }
}

HRESULT WINAPI PropVariantToUInt32Vector(REFPROPVARIANT propvar, ULONG *buffer, ULONG size, ULONG *count)
{
    TRACE("%p, %p, %lu, %p.\n", propvar, buffer, size, count);

    *count = 0;

    switch (propvar->vt)
    {
    case VT_EMPTY:
        return E_INVALIDARG;
    default:
        FIXME("Unimplemented for type %d.\n", propvar->vt);
        return E_NOTIMPL;
    }
    return E_NOTIMPL;
}

#ifdef __REACTOS__
static ULONG serialized_align(ULONG cb)
{
    return (cb + 3) & ~3u;
}

static HRESULT serialized_value_size(const PROPVARIANT *propvar, ULONG *size)
{
    ULONG i, cb;

    switch (propvar->vt)
    {
    case VT_EMPTY:
    case VT_NULL:
        *size = 0;
        return S_OK;
    case VT_I1:
    case VT_UI1:
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_R4:
    case VT_ERROR:
        *size = 4;
        return S_OK;
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_FILETIME:
        *size = 8;
        return S_OK;
    case VT_CLSID:
        if (!propvar->puuid)
            return E_INVALIDARG;
        *size = sizeof(GUID);
        return S_OK;
    case VT_LPWSTR:
        cb = propvar->pwszVal ? (lstrlenW(propvar->pwszVal) + 1) * sizeof(WCHAR) : sizeof(WCHAR);
        *size = sizeof(DWORD) + serialized_align(cb);
        return S_OK;
    case VT_LPSTR:
        cb = propvar->pszVal ? lstrlenA(propvar->pszVal) + 1 : 1;
        *size = sizeof(DWORD) + serialized_align(cb);
        return S_OK;
    case VT_BLOB:
        *size = sizeof(DWORD) + serialized_align(propvar->blob.cbSize);
        return S_OK;
    case VT_VECTOR | VT_LPWSTR:
        cb = sizeof(DWORD);
        for (i = 0; i < propvar->calpwstr.cElems; i++)
        {
            ULONG cch = propvar->calpwstr.pElems[i] ? lstrlenW(propvar->calpwstr.pElems[i]) + 1 : 1;
            cb += sizeof(DWORD) + serialized_align(cch * sizeof(WCHAR));
        }
        *size = cb;
        return S_OK;
    default:
        FIXME("Unsupported type %d.\n", propvar->vt);
        return E_INVALIDARG;
    }
}

static BYTE *serialize_string(BYTE *ptr, const void *data, ULONG count, ULONG cb)
{
    *(DWORD *)ptr = count;
    ptr += sizeof(DWORD);
    memset(ptr, 0, serialized_align(cb));
    if (data)
        memcpy(ptr, data, cb);
    return ptr + serialized_align(cb);
}

HRESULT WINAPI StgSerializePropVariant(const PROPVARIANT *propvar, SERIALIZEDPROPERTYVALUE **ret, ULONG *size)
{
    SERIALIZEDPROPERTYVALUE *prop;
    ULONG cbValue, i;
    HRESULT hr;
    BYTE *ptr;

    TRACE("(%p, %p, %p)\n", propvar, ret, size);

    if (!propvar || !ret || !size)
        return E_INVALIDARG;
    *ret = NULL;
    *size = 0;

    hr = serialized_value_size(propvar, &cbValue);
    if (FAILED(hr))
        return hr;

    prop = CoTaskMemAlloc(sizeof(DWORD) + cbValue);
    if (!prop)
        return E_OUTOFMEMORY;
    prop->dwType = propvar->vt;
    ptr = (BYTE *)prop + sizeof(DWORD);
    memset(ptr, 0, cbValue);

    switch (propvar->vt)
    {
    case VT_EMPTY:
    case VT_NULL:
        break;
    case VT_I1:
    case VT_UI1:
        *ptr = propvar->bVal;
        break;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
        *(USHORT *)ptr = propvar->uiVal;
        break;
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_R4:
    case VT_ERROR:
        *(ULONG *)ptr = propvar->ulVal;
        break;
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_FILETIME:
        memcpy(ptr, &propvar->uhVal, sizeof(ULONGLONG));
        break;
    case VT_CLSID:
        memcpy(ptr, propvar->puuid, sizeof(GUID));
        break;
    case VT_LPWSTR:
        if (propvar->pwszVal)
            serialize_string(ptr, propvar->pwszVal, lstrlenW(propvar->pwszVal) + 1, (lstrlenW(propvar->pwszVal) + 1) * sizeof(WCHAR));
        else
            serialize_string(ptr, NULL, 1, sizeof(WCHAR));
        break;
    case VT_LPSTR:
        if (propvar->pszVal)
            serialize_string(ptr, propvar->pszVal, lstrlenA(propvar->pszVal) + 1, lstrlenA(propvar->pszVal) + 1);
        else
            serialize_string(ptr, NULL, 1, 1);
        break;
    case VT_BLOB:
        serialize_string(ptr, propvar->blob.pBlobData, propvar->blob.cbSize, propvar->blob.cbSize);
        break;
    case VT_VECTOR | VT_LPWSTR:
        *(DWORD *)ptr = propvar->calpwstr.cElems;
        ptr += sizeof(DWORD);
        for (i = 0; i < propvar->calpwstr.cElems; i++)
        {
            const WCHAR *str = propvar->calpwstr.pElems[i];
            ULONG cch = str ? lstrlenW(str) + 1 : 1;
            ptr = serialize_string(ptr, str, cch, str ? cch * sizeof(WCHAR) : sizeof(WCHAR));
        }
        break;
    }

    *ret = prop;
    *size = sizeof(DWORD) + cbValue;
    return S_OK;
}

static HRESULT deserialize_string(const BYTE **ptr, const BYTE *end, ULONG elemsize, void **data, ULONG *count)
{
    ULONG n, cb;

    if (end - *ptr < (LONG_PTR)sizeof(DWORD))
        return E_INVALIDARG;
    memcpy(&n, *ptr, sizeof(n));
    *ptr += sizeof(DWORD);
    if (n > (ULONG)(end - *ptr) / elemsize)
        return E_INVALIDARG;
    cb = n * elemsize;
    *data = CoTaskMemAlloc(max(cb, elemsize));
    if (!*data)
        return E_OUTOFMEMORY;
    memset(*data, 0, max(cb, elemsize));
    memcpy(*data, *ptr, cb);
    *ptr += min(serialized_align(cb), (ULONG)(end - *ptr));
    *count = n;
    return S_OK;
}

HRESULT WINAPI StgDeserializePropVariant(const SERIALIZEDPROPERTYVALUE *prop, ULONG max_size, PROPVARIANT *propvar)
{
    const BYTE *ptr, *end;
    ULONG count, i, need;
    DWORD type;
    void *data;
    HRESULT hr;

    TRACE("(%p, %lu, %p)\n", prop, max_size, propvar);

    if (!prop || !propvar || max_size < sizeof(DWORD))
        return E_INVALIDARG;

    PropVariantInit(propvar);
    ptr = (const BYTE *)prop + sizeof(DWORD);
    end = (const BYTE *)prop + max_size;
    memcpy(&type, prop, sizeof(type));
    propvar->vt = (VARTYPE)type;

    switch (propvar->vt)
    {
    case VT_EMPTY:
    case VT_NULL:
        return S_OK;
    case VT_I1:
    case VT_UI1:
        need = 1;
        break;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
        need = 2;
        break;
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_R4:
    case VT_ERROR:
        need = 4;
        break;
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_FILETIME:
        need = 8;
        break;
    case VT_CLSID:
        if (end - ptr < (LONG_PTR)sizeof(GUID))
            goto invalid;
        propvar->puuid = CoTaskMemAlloc(sizeof(GUID));
        if (!propvar->puuid)
            goto nomem;
        memcpy(propvar->puuid, ptr, sizeof(GUID));
        return S_OK;
    case VT_LPWSTR:
        hr = deserialize_string(&ptr, end, sizeof(WCHAR), &data, &count);
        if (FAILED(hr))
        {
            propvar->vt = VT_EMPTY;
            return hr;
        }
        propvar->pwszVal = data;
        return S_OK;
    case VT_LPSTR:
        hr = deserialize_string(&ptr, end, 1, &data, &count);
        if (FAILED(hr))
        {
            propvar->vt = VT_EMPTY;
            return hr;
        }
        propvar->pszVal = data;
        return S_OK;
    case VT_BLOB:
        hr = deserialize_string(&ptr, end, 1, &data, &count);
        if (FAILED(hr))
        {
            propvar->vt = VT_EMPTY;
            return hr;
        }
        propvar->blob.pBlobData = data;
        propvar->blob.cbSize = count;
        return S_OK;
    case VT_VECTOR | VT_LPWSTR:
        if (end - ptr < (LONG_PTR)sizeof(DWORD))
            goto invalid;
        memcpy(&count, ptr, sizeof(count));
        ptr += sizeof(DWORD);
        if (count > (ULONG)(end - ptr) / sizeof(DWORD))
            goto invalid;
        propvar->calpwstr.pElems = CoTaskMemAlloc(max(count, 1) * sizeof(WCHAR *));
        if (!propvar->calpwstr.pElems)
            goto nomem;
        propvar->calpwstr.cElems = 0;
        for (i = 0; i < count; i++)
        {
            ULONG cch;
            hr = deserialize_string(&ptr, end, sizeof(WCHAR), &data, &cch);
            if (FAILED(hr))
            {
                PropVariantClear(propvar);
                return hr;
            }
            propvar->calpwstr.pElems[propvar->calpwstr.cElems++] = data;
        }
        return S_OK;
    default:
        FIXME("Unsupported type %d.\n", propvar->vt);
        goto invalid;
    }

    if ((ULONG)(end - ptr) < need)
        goto invalid;
    memcpy(&propvar->uhVal, ptr, need);
    return S_OK;

invalid:
    PropVariantInit(propvar);
    return E_INVALIDARG;
nomem:
    PropVariantInit(propvar);
    return E_OUTOFMEMORY;
}
#endif
