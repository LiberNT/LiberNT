/*
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

#ifndef __WINE_PROPVARUTIL_H
#define __WINE_PROPVARUTIL_H

#include <shtypes.h>
#include <shlwapi.h>

#ifndef WINE_NTSTATUS_DECLARED
#define WINE_NTSTATUS_DECLARED
typedef LONG NTSTATUS;
#endif

#ifndef PSSTDAPI
#ifdef _PROPSYS_
#define PSSTDAPI          STDAPI
#define PSSTDAPI_(type)   STDAPI_(type)
#else
#define PSSTDAPI          DECLSPEC_IMPORT STDAPI
#define PSSTDAPI_(type)   DECLSPEC_IMPORT STDAPI_(type)
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum tagPROPVAR_CHANGE_FLAGS
{
    PVCHF_DEFAULT           = 0x00000000,
    PVCHF_NOVALUEPROP       = 0x00000001,
    PVCHF_ALPHABOOL         = 0x00000002,
    PVCHF_NOUSEROVERRIDE    = 0x00000004,
    PVCHF_LOCALBOOL         = 0x00000008,
    PVCHF_NOHEXSTRING       = 0x00000010,
};

typedef int PROPVAR_CHANGE_FLAGS;

enum tagPROPVAR_COMPARE_UNIT
{
    PVCU_DEFAULT           = 0x00000000,
    PVCU_SECOND            = 0x00000001,
    PVCU_MINUTE            = 0x00000002,
    PVCU_HOUR              = 0x00000003,
    PVCU_DAY               = 0x00000004,
    PVCU_MONTH             = 0x00000005,
    PVCU_YEAR              = 0x00000006,
};

typedef int PROPVAR_COMPARE_UNIT;

enum tagPROPVAR_COMPARE_FLAGS
{
    PVCF_DEFAULT           = 0x00000000,
    PVCF_TREATEMPTYASGREATERTHAN = 0x00000001,
    PVCF_USESTRCMP         = 0x00000002,
    PVCF_USESTRCMPC        = 0x00000004,
    PVCF_USESTRCMPI        = 0x00000008,
    PVCF_USESTRCMPIC       = 0x00000010,
    PVCF_DIGITSASNUMBERS_CASESENSITIVE = 0x20,
};

typedef int PROPVAR_COMPARE_FLAGS;

enum tagPSTIME_FLAGS
{
    PSTF_UTC   = 0x00000000,
    PSTF_LOCAL = 0x00000001,
};

typedef int PSTIME_FLAGS;

PSSTDAPI PropVariantChangeType(PROPVARIANT *ppropvarDest, REFPROPVARIANT propvarSrc,
                                     PROPVAR_CHANGE_FLAGS flags, VARTYPE vt);
PSSTDAPI InitPropVariantFromGUIDAsString(REFGUID guid, PROPVARIANT *ppropvar);
PSSTDAPI InitVariantFromFileTime(const FILETIME *ft, VARIANT *var);
PSSTDAPI InitVariantFromGUIDAsString(REFGUID guid, VARIANT *pvar);
PSSTDAPI InitPropVariantFromBuffer(const VOID *pv, UINT cb, PROPVARIANT *ppropvar);
PSSTDAPI InitPropVariantFromCLSID(REFCLSID clsid, PROPVARIANT *ppropvar);
PSSTDAPI InitVariantFromBuffer(const VOID *pv, UINT cb, VARIANT *pvar);
PSSTDAPI VariantToGUID(const VARIANT *pvar, GUID *guid);
PSSTDAPI_(INT) PropVariantCompareEx(REFPROPVARIANT propvar1, REFPROPVARIANT propvar2,
                                PROPVAR_COMPARE_UNIT uint, PROPVAR_COMPARE_FLAGS flags);
PSSTDAPI InitPropVariantFromFileTime(const FILETIME *pftIn, PROPVARIANT *ppropvar);
PSSTDAPI InitPropVariantFromStringVector(PCWSTR *strs, ULONG count, PROPVARIANT *ppropvar);

PSSTDAPI PropVariantToDouble(REFPROPVARIANT propvarIn, double *ret);
PSSTDAPI PropVariantToFileTime(REFPROPVARIANT propvar, PSTIME_FLAGS flags, FILETIME *out);
PSSTDAPI PropVariantToGUID(REFPROPVARIANT ppropvar, GUID *guid);
PSSTDAPI PropVariantToInt16(REFPROPVARIANT propvarIn, SHORT *ret);
PSSTDAPI PropVariantToInt32(REFPROPVARIANT propvarIn, LONG *ret);
PSSTDAPI PropVariantToInt64(REFPROPVARIANT propvarIn, LONGLONG *ret);
PSSTDAPI PropVariantToUInt16(REFPROPVARIANT propvarIn, USHORT *ret);
PSSTDAPI PropVariantToUInt32(REFPROPVARIANT propvarIn, ULONG *ret);
PSSTDAPI PropVariantToUInt32Vector(REFPROPVARIANT propvar, ULONG *buffer, ULONG size, ULONG *count);
PSSTDAPI_(ULONG) PropVariantToUInt32WithDefault(REFPROPVARIANT propvarIn, ULONG uLDefault);
PSSTDAPI PropVariantToUInt64(REFPROPVARIANT propvarIn, ULONGLONG *ret);
PSSTDAPI PropVariantToBoolean(REFPROPVARIANT propvarIn, BOOL *ret);
PSSTDAPI PropVariantToBSTR(REFPROPVARIANT propvar, BSTR *bstr);
PSSTDAPI PropVariantToBuffer(REFPROPVARIANT propvarIn, void *ret, UINT cb);
PSSTDAPI PropVariantToString(REFPROPVARIANT propvarIn, PWSTR ret, UINT cch);
PSSTDAPI_(PCWSTR) PropVariantToStringWithDefault(REFPROPVARIANT propvarIn, LPCWSTR pszDefault);
PSSTDAPI_(PCWSTR) VariantToStringWithDefault(const VARIANT *pvar, LPCWSTR pszDefault);
PSSTDAPI VariantToString(REFVARIANT var, PWSTR ret, UINT cch);

PSSTDAPI PropVariantToStringAlloc(REFPROPVARIANT propvarIn, WCHAR **ret);

PSSTDAPI PropVariantToVariant(const PROPVARIANT *propvar, VARIANT *var);
PSSTDAPI VariantToPropVariant(const VARIANT* var, PROPVARIANT* propvar);

PSSTDAPI PropVariantGetStringElem(const PROPVARIANT *propvar, ULONG idx, WCHAR **ret);

#ifdef __cplusplus

HRESULT InitPropVariantFromBoolean(BOOL fVal, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromInt16(SHORT nVal, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromUInt16(USHORT uiVal, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromInt32(LONG lVal, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromUInt32(ULONG ulVal, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromInt64(LONGLONG llVal, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromUInt64(ULONGLONG ullVal, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromDouble(DOUBLE dblVal, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromString(PCWSTR psz, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromGUIDAsBuffer(REFGUID guid, PROPVARIANT *ppropvar);
BOOL IsPropVariantVector(REFPROPVARIANT propvar);
BOOL IsPropVariantString(REFPROPVARIANT propvar);

#ifndef NO_PROPVAR_INLINES

inline HRESULT InitPropVariantFromBoolean(BOOL fVal, PROPVARIANT *ppropvar)
{
    ppropvar->vt = VT_BOOL;
    ppropvar->boolVal = fVal ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

inline HRESULT InitPropVariantFromInt16(SHORT nVal, PROPVARIANT *ppropvar)
{
    ppropvar->vt = VT_I2;
    ppropvar->iVal = nVal;
    return S_OK;
}

inline HRESULT InitPropVariantFromUInt16(USHORT uiVal, PROPVARIANT *ppropvar)
{
    ppropvar->vt = VT_UI2;
    ppropvar->uiVal = uiVal;
    return S_OK;
}

inline HRESULT InitPropVariantFromInt32(LONG lVal, PROPVARIANT *ppropvar)
{
    ppropvar->vt = VT_I4;
    ppropvar->lVal = lVal;
    return S_OK;
}

inline HRESULT InitPropVariantFromUInt32(ULONG ulVal, PROPVARIANT *ppropvar)
{
    ppropvar->vt = VT_UI4;
    ppropvar->ulVal = ulVal;
    return S_OK;
}

inline HRESULT InitPropVariantFromInt64(LONGLONG llVal, PROPVARIANT *ppropvar)
{
    ppropvar->vt = VT_I8;
    ppropvar->hVal.QuadPart = llVal;
    return S_OK;
}

inline HRESULT InitPropVariantFromUInt64(ULONGLONG ullVal, PROPVARIANT *ppropvar)
{
    ppropvar->vt = VT_UI8;
    ppropvar->uhVal.QuadPart = ullVal;
    return S_OK;
}

inline HRESULT InitPropVariantFromDouble(DOUBLE dblVal, PROPVARIANT *ppropvar)
{
    ppropvar->vt = VT_R8;
    ppropvar->dblVal = dblVal;
    return S_OK;
}

inline HRESULT InitPropVariantFromString(PCWSTR psz, PROPVARIANT *ppropvar)
{
    HRESULT hres;

    hres = SHStrDupW(psz, &ppropvar->pwszVal);
    if(SUCCEEDED(hres))
        ppropvar->vt = VT_LPWSTR;
    else
        PropVariantInit(ppropvar);

    return hres;
}

inline HRESULT InitPropVariantFromGUIDAsBuffer(REFGUID guid, PROPVARIANT *ppropvar)
{
#ifdef __cplusplus
    return InitPropVariantFromBuffer(&guid, sizeof(GUID), ppropvar);
#else
    return InitPropVariantFromBuffer(guid, sizeof(GUID), ppropvar);
#endif
}

inline BOOL IsPropVariantVector(REFPROPVARIANT propvar)
{
    return (propvar.vt & (VT_ARRAY | VT_VECTOR));
}

inline BOOL IsPropVariantString(REFPROPVARIANT propvar)
{
    return (PropVariantToStringWithDefault(propvar, NULL) != NULL);
}

#endif /* NO_PROPVAR_INLINES */
#endif /* __cplusplus */

PSSTDAPI StgSerializePropVariant(const PROPVARIANT *ppropvar, SERIALIZEDPROPERTYVALUE **ppprop, ULONG *pcb);
PSSTDAPI StgDeserializePropVariant(const SERIALIZEDPROPERTYVALUE *pprop, ULONG cbmax, PROPVARIANT *ppropvar);

#ifdef __cplusplus
}
#endif

typedef enum DRAWPROGRESSFLAGS {
    DPF_NONE = 0,
    DPF_MARQUEE = 1,
    DPF_MARQUEE_COMPLETE = 2,
    DPF_ERROR = 4,
    DPF_WARNING = 8,
    DPF_STOPPED = 16
} DRAWPROGRESSFLAGS;

#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI InitVariantFromResource(HINSTANCE hinst, UINT id, VARIANT * pvar);
int WINAPI VariantCompare(const VARIANT *const var1, const VARIANT *const var2);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
void ClearPropVariantArray(PROPVARIANT *rgPropVar, UINT cVars);
void ClearVariantArray(VARIANT *pvars, UINT cvars);
HRESULT InitPropVariantFromBooleanVector(const BOOL *prgf, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromDoubleVector(const DOUBLE *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromFileTimeVector(const FILETIME *prgft, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromInt16Vector(const SHORT *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromInt32Vector(const LONG *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromInt64Vector(const LONGLONG *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromPropVariantVectorElem(const PROPVARIANT *const propvarIn, ULONG iElem, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromResource(HINSTANCE hinst, UINT id, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromStringAsVector(PCWSTR psz, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromStrRet(STRRET *pstrret, LPCITEMIDLIST pidl, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromUInt16Vector(const USHORT *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromUInt32Vector(const ULONG *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT InitPropVariantFromUInt64Vector(const ULONGLONG *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT InitPropVariantVectorFromPropVariant(const PROPVARIANT *const propvarSingle, PROPVARIANT *ppropvarVector);
HRESULT InitVariantFromBooleanArray(const BOOL *prgf, ULONG cElems, VARIANT *pvar);
HRESULT InitVariantFromDoubleArray(const DOUBLE *prgn, ULONG cElems, VARIANT *pvar);
HRESULT InitVariantFromFileTimeArray(const FILETIME *prgft, ULONG cElems, VARIANT *pvar);
HRESULT InitVariantFromInt16Array(const SHORT *prgn, ULONG cElems, VARIANT *pvar);
HRESULT InitVariantFromInt32Array(const LONG *prgn, ULONG cElems, VARIANT *pvar);
HRESULT InitVariantFromInt64Array(const LONGLONG *prgn, ULONG cElems, VARIANT *pvar);
HRESULT InitVariantFromStringArray(PCWSTR *prgsz, ULONG cElems, VARIANT *pvar);
HRESULT InitVariantFromStrRet(STRRET *pstrret, LPCITEMIDLIST pidl, VARIANT *pvar);
HRESULT InitVariantFromUInt16Array(const USHORT *prgn, ULONG cElems, VARIANT *pvar);
HRESULT InitVariantFromUInt32Array(const ULONG *prgn, ULONG cElems, VARIANT *pvar);
HRESULT InitVariantFromUInt64Array(const ULONGLONG *prgn, ULONG cElems, VARIANT *pvar);
HRESULT InitVariantFromVariantArrayElem(const VARIANT *const varIn, ULONG iElem, VARIANT *pvar);
HRESULT PropVariantGetBooleanElem(const PROPVARIANT *const propvar, ULONG iElem, BOOL *pfVal);
HRESULT PropVariantGetDoubleElem(const PROPVARIANT *const propvar, ULONG iElem, DOUBLE *pnVal);
ULONG PropVariantGetElementCount(const PROPVARIANT *const propvar);
HRESULT PropVariantGetFileTimeElem(const PROPVARIANT *const propvar, ULONG iElem, FILETIME *pftVal);
HRESULT PropVariantGetInt16Elem(const PROPVARIANT *const propvar, ULONG iElem, SHORT *pnVal);
HRESULT PropVariantGetInt32Elem(const PROPVARIANT *const propvar, ULONG iElem, LONG *pnVal);
HRESULT PropVariantGetInt64Elem(const PROPVARIANT *const propvar, ULONG iElem, LONGLONG *pnVal);
HRESULT PropVariantGetUInt16Elem(const PROPVARIANT *const propvar, ULONG iElem, USHORT *pnVal);
HRESULT PropVariantGetUInt32Elem(const PROPVARIANT *const propvar, ULONG iElem, ULONG *pnVal);
HRESULT PropVariantGetUInt64Elem(const PROPVARIANT *const propvar, ULONG iElem, ULONGLONG *pnVal);
HRESULT PropVariantToBooleanVector(const PROPVARIANT *const propvar, BOOL *prgf, ULONG crgf, ULONG *pcElem);
HRESULT PropVariantToBooleanVectorAlloc(const PROPVARIANT *const propvar, BOOL **pprgf, ULONG *pcElem);
BOOL PropVariantToBooleanWithDefault(const PROPVARIANT *const propvarIn, BOOL fDefault);
HRESULT PropVariantToDoubleVector(const PROPVARIANT *const propvar, DOUBLE *prgn, ULONG crgn, ULONG *pcElem);
HRESULT PropVariantToDoubleVectorAlloc(const PROPVARIANT *const propvar, DOUBLE **pprgn, ULONG *pcElem);
DOUBLE PropVariantToDoubleWithDefault(const PROPVARIANT *const propvarIn, DOUBLE dblDefault);
HRESULT PropVariantToFileTimeVector(const PROPVARIANT *const propvar, FILETIME *prgft, ULONG crgft, ULONG *pcElem);
HRESULT PropVariantToFileTimeVectorAlloc(const PROPVARIANT *const propvar, FILETIME **pprgft, ULONG *pcElem);
HRESULT PropVariantToInt16Vector(const PROPVARIANT *const propvar, SHORT *prgn, ULONG crgn, ULONG *pcElem);
HRESULT PropVariantToInt16VectorAlloc(const PROPVARIANT *const propvar, SHORT **pprgn, ULONG *pcElem);
SHORT PropVariantToInt16WithDefault(const PROPVARIANT *const propvarIn, SHORT iDefault);
HRESULT PropVariantToInt32Vector(const PROPVARIANT *const propvar, LONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT PropVariantToInt32VectorAlloc(const PROPVARIANT *const propvar, LONG **pprgn, ULONG *pcElem);
LONG PropVariantToInt32WithDefault(const PROPVARIANT *const propvarIn, LONG lDefault);
HRESULT PropVariantToInt64Vector(const PROPVARIANT *const propvar, LONGLONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT PropVariantToInt64VectorAlloc(const PROPVARIANT *const propvar, LONGLONG **pprgn, ULONG *pcElem);
LONGLONG PropVariantToInt64WithDefault(const PROPVARIANT *const propvarIn, LONGLONG llDefault);
HRESULT PropVariantToStringVector(const PROPVARIANT *const propvar, PWSTR *prgsz, ULONG crgsz, ULONG *pcElem);
HRESULT PropVariantToStringVectorAlloc(const PROPVARIANT *const propvar, PWSTR **pprgsz, ULONG *pcElem);
HRESULT PropVariantToStrRet(const PROPVARIANT *const propvar, STRRET *pstrret);
HRESULT PropVariantToUInt16Vector(const PROPVARIANT *const propvar, USHORT *prgn, ULONG crgn, ULONG *pcElem);
HRESULT PropVariantToUInt16VectorAlloc(const PROPVARIANT *const propvar, USHORT **pprgn, ULONG *pcElem);
USHORT PropVariantToUInt16WithDefault(const PROPVARIANT *const propvarIn, USHORT uiDefault);
HRESULT PropVariantToUInt32VectorAlloc(const PROPVARIANT *const propvar, ULONG **pprgn, ULONG *pcElem);
HRESULT PropVariantToUInt64Vector(const PROPVARIANT *const propvar, ULONGLONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT PropVariantToUInt64VectorAlloc(const PROPVARIANT *const propvar, ULONGLONG **pprgn, ULONG *pcElem);
ULONGLONG PropVariantToUInt64WithDefault(const PROPVARIANT *const propvarIn, ULONGLONG ullDefault);
HRESULT VariantGetBooleanElem(const VARIANT *const var, ULONG iElem, BOOL *pfVal);
HRESULT VariantGetDoubleElem(const VARIANT *const var, ULONG iElem, DOUBLE *pnVal);
ULONG VariantGetElementCount(const VARIANT *const varIn);
HRESULT VariantGetInt16Elem(const VARIANT *const var, ULONG iElem, SHORT *pnVal);
HRESULT VariantGetInt32Elem(const VARIANT *const var, ULONG iElem, LONG *pnVal);
HRESULT VariantGetInt64Elem(const VARIANT *const var, ULONG iElem, LONGLONG *pnVal);
HRESULT VariantGetStringElem(const VARIANT *const var, ULONG iElem, PWSTR *ppszVal);
HRESULT VariantGetUInt16Elem(const VARIANT *const var, ULONG iElem, USHORT *pnVal);
HRESULT VariantGetUInt32Elem(const VARIANT *const var, ULONG iElem, ULONG *pnVal);
HRESULT VariantGetUInt64Elem(const VARIANT *const var, ULONG iElem, ULONGLONG *pnVal);
HRESULT VariantToBoolean(const VARIANT *const varIn, BOOL *pfRet);
HRESULT VariantToBooleanArray(const VARIANT *const var, BOOL *prgf, ULONG crgn, ULONG *pcElem);
HRESULT VariantToBooleanArrayAlloc(const VARIANT *const var, BOOL **pprgf, ULONG *pcElem);
BOOL VariantToBooleanWithDefault(const VARIANT *const varIn, BOOL fDefault);
HRESULT VariantToBuffer(const VARIANT *const varIn, void *pv, UINT cb);
HRESULT VariantToDosDateTime(const VARIANT *const varIn, WORD *pwDate, WORD *pwTime);
HRESULT VariantToDouble(const VARIANT *const varIn, DOUBLE *pdblRet);
HRESULT VariantToDoubleArray(const VARIANT *const var, DOUBLE *prgn, ULONG crgn, ULONG *pcElem);
HRESULT VariantToDoubleArrayAlloc(const VARIANT *const var, DOUBLE **pprgn, ULONG *pcElem);
DOUBLE VariantToDoubleWithDefault(const VARIANT *const varIn, DOUBLE dblDefault);
HRESULT VariantToFileTime(const VARIANT *const varIn, PSTIME_FLAGS stfOut, FILETIME *pftOut);
HRESULT VariantToInt16(const VARIANT *const varIn, SHORT *piRet);
HRESULT VariantToInt16Array(const VARIANT *const var, SHORT *prgn, ULONG crgn, ULONG *pcElem);
HRESULT VariantToInt16ArrayAlloc(const VARIANT *const var, SHORT **pprgn, ULONG *pcElem);
SHORT VariantToInt16WithDefault(const VARIANT *const varIn, SHORT iDefault);
HRESULT VariantToInt32(const VARIANT *const varIn, LONG *plRet);
HRESULT VariantToInt32Array(const VARIANT *const var, LONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT VariantToInt32ArrayAlloc(const VARIANT *const var, LONG **pprgn, ULONG *pcElem);
LONG VariantToInt32WithDefault(const VARIANT *const varIn, LONG lDefault);
HRESULT VariantToInt64(const VARIANT *const varIn, LONGLONG *pllRet);
HRESULT VariantToInt64Array(const VARIANT *const var, LONGLONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT VariantToInt64ArrayAlloc(const VARIANT *const var, LONGLONG **pprgn, ULONG *pcElem);
LONGLONG VariantToInt64WithDefault(const VARIANT *const varIn, LONGLONG llDefault);
HRESULT VariantToStringAlloc(const VARIANT *const varIn, PWSTR *ppszBuf);
HRESULT VariantToStringArray(const VARIANT *const var, PWSTR *prgsz, ULONG crgsz, ULONG *pcElem);
HRESULT VariantToStringArrayAlloc(const VARIANT *const var, PWSTR **pprgsz, ULONG *pcElem);
HRESULT VariantToStrRet(const VARIANT *const varIn, STRRET *pstrret);
HRESULT VariantToUInt16(const VARIANT *const varIn, USHORT *puiRet);
HRESULT VariantToUInt16Array(const VARIANT *const var, USHORT *prgn, ULONG crgn, ULONG *pcElem);
HRESULT VariantToUInt16ArrayAlloc(const VARIANT *const var, USHORT **pprgn, ULONG *pcElem);
USHORT VariantToUInt16WithDefault(const VARIANT *const varIn, USHORT uiDefault);
HRESULT VariantToUInt32(const VARIANT *const varIn, ULONG *pulRet);
HRESULT VariantToUInt32Array(const VARIANT *const var, ULONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT VariantToUInt32ArrayAlloc(const VARIANT *const var, ULONG **pprgn, ULONG *pcElem);
ULONG VariantToUInt32WithDefault(const VARIANT *const varIn, ULONG ulDefault);
HRESULT VariantToUInt64(const VARIANT *const varIn, ULONGLONG *pullRet);
HRESULT VariantToUInt64Array(const VARIANT *const var, ULONGLONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT VariantToUInt64ArrayAlloc(const VARIANT *const var, ULONGLONG **pprgn, ULONG *pcElem);
ULONGLONG VariantToUInt64WithDefault(const VARIANT *const varIn, ULONGLONG ullDefault);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_PROPVARUTIL_H */
