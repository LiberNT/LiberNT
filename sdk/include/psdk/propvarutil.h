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
void STDAPICALLTYPE ClearPropVariantArray(PROPVARIANT *rgPropVar, UINT cVars);
void STDAPICALLTYPE ClearVariantArray(VARIANT *pvars, UINT cvars);
HRESULT STDAPICALLTYPE InitPropVariantFromBooleanVector(const BOOL *prgf, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromDoubleVector(const DOUBLE *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromFileTimeVector(const FILETIME *prgft, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromInt16Vector(const SHORT *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromInt32Vector(const LONG *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromInt64Vector(const LONGLONG *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromPropVariantVectorElem(const PROPVARIANT *const propvarIn, ULONG iElem, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromResource(HINSTANCE hinst, UINT id, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromStringAsVector(PCWSTR psz, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromStrRet(STRRET *pstrret, LPCITEMIDLIST pidl, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromUInt16Vector(const USHORT *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromUInt32Vector(const ULONG *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantFromUInt64Vector(const ULONGLONG *prgn, ULONG cElems, PROPVARIANT *ppropvar);
HRESULT STDAPICALLTYPE InitPropVariantVectorFromPropVariant(const PROPVARIANT *const propvarSingle, PROPVARIANT *ppropvarVector);
HRESULT STDAPICALLTYPE InitVariantFromBooleanArray(const BOOL *prgf, ULONG cElems, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromDoubleArray(const DOUBLE *prgn, ULONG cElems, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromFileTimeArray(const FILETIME *prgft, ULONG cElems, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromInt16Array(const SHORT *prgn, ULONG cElems, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromInt32Array(const LONG *prgn, ULONG cElems, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromInt64Array(const LONGLONG *prgn, ULONG cElems, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromStringArray(PCWSTR *prgsz, ULONG cElems, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromStrRet(STRRET *pstrret, LPCITEMIDLIST pidl, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromUInt16Array(const USHORT *prgn, ULONG cElems, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromUInt32Array(const ULONG *prgn, ULONG cElems, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromUInt64Array(const ULONGLONG *prgn, ULONG cElems, VARIANT *pvar);
HRESULT STDAPICALLTYPE InitVariantFromVariantArrayElem(const VARIANT *const varIn, ULONG iElem, VARIANT *pvar);
HRESULT STDAPICALLTYPE PropVariantGetBooleanElem(const PROPVARIANT *const propvar, ULONG iElem, BOOL *pfVal);
HRESULT STDAPICALLTYPE PropVariantGetDoubleElem(const PROPVARIANT *const propvar, ULONG iElem, DOUBLE *pnVal);
ULONG STDAPICALLTYPE PropVariantGetElementCount(const PROPVARIANT *const propvar);
HRESULT STDAPICALLTYPE PropVariantGetFileTimeElem(const PROPVARIANT *const propvar, ULONG iElem, FILETIME *pftVal);
HRESULT STDAPICALLTYPE PropVariantGetInt16Elem(const PROPVARIANT *const propvar, ULONG iElem, SHORT *pnVal);
HRESULT STDAPICALLTYPE PropVariantGetInt32Elem(const PROPVARIANT *const propvar, ULONG iElem, LONG *pnVal);
HRESULT STDAPICALLTYPE PropVariantGetInt64Elem(const PROPVARIANT *const propvar, ULONG iElem, LONGLONG *pnVal);
HRESULT STDAPICALLTYPE PropVariantGetUInt16Elem(const PROPVARIANT *const propvar, ULONG iElem, USHORT *pnVal);
HRESULT STDAPICALLTYPE PropVariantGetUInt32Elem(const PROPVARIANT *const propvar, ULONG iElem, ULONG *pnVal);
HRESULT STDAPICALLTYPE PropVariantGetUInt64Elem(const PROPVARIANT *const propvar, ULONG iElem, ULONGLONG *pnVal);
HRESULT STDAPICALLTYPE PropVariantToBooleanVector(const PROPVARIANT *const propvar, BOOL *prgf, ULONG crgf, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToBooleanVectorAlloc(const PROPVARIANT *const propvar, BOOL **pprgf, ULONG *pcElem);
BOOL STDAPICALLTYPE PropVariantToBooleanWithDefault(const PROPVARIANT *const propvarIn, BOOL fDefault);
HRESULT STDAPICALLTYPE PropVariantToDoubleVector(const PROPVARIANT *const propvar, DOUBLE *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToDoubleVectorAlloc(const PROPVARIANT *const propvar, DOUBLE **pprgn, ULONG *pcElem);
DOUBLE STDAPICALLTYPE PropVariantToDoubleWithDefault(const PROPVARIANT *const propvarIn, DOUBLE dblDefault);
HRESULT STDAPICALLTYPE PropVariantToFileTimeVector(const PROPVARIANT *const propvar, FILETIME *prgft, ULONG crgft, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToFileTimeVectorAlloc(const PROPVARIANT *const propvar, FILETIME **pprgft, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToInt16Vector(const PROPVARIANT *const propvar, SHORT *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToInt16VectorAlloc(const PROPVARIANT *const propvar, SHORT **pprgn, ULONG *pcElem);
SHORT STDAPICALLTYPE PropVariantToInt16WithDefault(const PROPVARIANT *const propvarIn, SHORT iDefault);
HRESULT STDAPICALLTYPE PropVariantToInt32Vector(const PROPVARIANT *const propvar, LONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToInt32VectorAlloc(const PROPVARIANT *const propvar, LONG **pprgn, ULONG *pcElem);
LONG STDAPICALLTYPE PropVariantToInt32WithDefault(const PROPVARIANT *const propvarIn, LONG lDefault);
HRESULT STDAPICALLTYPE PropVariantToInt64Vector(const PROPVARIANT *const propvar, LONGLONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToInt64VectorAlloc(const PROPVARIANT *const propvar, LONGLONG **pprgn, ULONG *pcElem);
LONGLONG STDAPICALLTYPE PropVariantToInt64WithDefault(const PROPVARIANT *const propvarIn, LONGLONG llDefault);
HRESULT STDAPICALLTYPE PropVariantToStringVector(const PROPVARIANT *const propvar, PWSTR *prgsz, ULONG crgsz, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToStringVectorAlloc(const PROPVARIANT *const propvar, PWSTR **pprgsz, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToStrRet(const PROPVARIANT *const propvar, STRRET *pstrret);
HRESULT STDAPICALLTYPE PropVariantToUInt16Vector(const PROPVARIANT *const propvar, USHORT *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToUInt16VectorAlloc(const PROPVARIANT *const propvar, USHORT **pprgn, ULONG *pcElem);
USHORT STDAPICALLTYPE PropVariantToUInt16WithDefault(const PROPVARIANT *const propvarIn, USHORT uiDefault);
HRESULT STDAPICALLTYPE PropVariantToUInt32VectorAlloc(const PROPVARIANT *const propvar, ULONG **pprgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToUInt64Vector(const PROPVARIANT *const propvar, ULONGLONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE PropVariantToUInt64VectorAlloc(const PROPVARIANT *const propvar, ULONGLONG **pprgn, ULONG *pcElem);
ULONGLONG STDAPICALLTYPE PropVariantToUInt64WithDefault(const PROPVARIANT *const propvarIn, ULONGLONG ullDefault);
HRESULT STDAPICALLTYPE VariantGetBooleanElem(const VARIANT *const var, ULONG iElem, BOOL *pfVal);
HRESULT STDAPICALLTYPE VariantGetDoubleElem(const VARIANT *const var, ULONG iElem, DOUBLE *pnVal);
ULONG STDAPICALLTYPE VariantGetElementCount(const VARIANT *const varIn);
HRESULT STDAPICALLTYPE VariantGetInt16Elem(const VARIANT *const var, ULONG iElem, SHORT *pnVal);
HRESULT STDAPICALLTYPE VariantGetInt32Elem(const VARIANT *const var, ULONG iElem, LONG *pnVal);
HRESULT STDAPICALLTYPE VariantGetInt64Elem(const VARIANT *const var, ULONG iElem, LONGLONG *pnVal);
HRESULT STDAPICALLTYPE VariantGetStringElem(const VARIANT *const var, ULONG iElem, PWSTR *ppszVal);
HRESULT STDAPICALLTYPE VariantGetUInt16Elem(const VARIANT *const var, ULONG iElem, USHORT *pnVal);
HRESULT STDAPICALLTYPE VariantGetUInt32Elem(const VARIANT *const var, ULONG iElem, ULONG *pnVal);
HRESULT STDAPICALLTYPE VariantGetUInt64Elem(const VARIANT *const var, ULONG iElem, ULONGLONG *pnVal);
HRESULT STDAPICALLTYPE VariantToBoolean(const VARIANT *const varIn, BOOL *pfRet);
HRESULT STDAPICALLTYPE VariantToBooleanArray(const VARIANT *const var, BOOL *prgf, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE VariantToBooleanArrayAlloc(const VARIANT *const var, BOOL **pprgf, ULONG *pcElem);
BOOL STDAPICALLTYPE VariantToBooleanWithDefault(const VARIANT *const varIn, BOOL fDefault);
HRESULT STDAPICALLTYPE VariantToBuffer(const VARIANT *const varIn, void *pv, UINT cb);
HRESULT STDAPICALLTYPE VariantToDosDateTime(const VARIANT *const varIn, WORD *pwDate, WORD *pwTime);
HRESULT STDAPICALLTYPE VariantToDouble(const VARIANT *const varIn, DOUBLE *pdblRet);
HRESULT STDAPICALLTYPE VariantToDoubleArray(const VARIANT *const var, DOUBLE *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE VariantToDoubleArrayAlloc(const VARIANT *const var, DOUBLE **pprgn, ULONG *pcElem);
DOUBLE STDAPICALLTYPE VariantToDoubleWithDefault(const VARIANT *const varIn, DOUBLE dblDefault);
HRESULT STDAPICALLTYPE VariantToFileTime(const VARIANT *const varIn, PSTIME_FLAGS stfOut, FILETIME *pftOut);
HRESULT STDAPICALLTYPE VariantToInt16(const VARIANT *const varIn, SHORT *piRet);
HRESULT STDAPICALLTYPE VariantToInt16Array(const VARIANT *const var, SHORT *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE VariantToInt16ArrayAlloc(const VARIANT *const var, SHORT **pprgn, ULONG *pcElem);
SHORT STDAPICALLTYPE VariantToInt16WithDefault(const VARIANT *const varIn, SHORT iDefault);
HRESULT STDAPICALLTYPE VariantToInt32(const VARIANT *const varIn, LONG *plRet);
HRESULT STDAPICALLTYPE VariantToInt32Array(const VARIANT *const var, LONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE VariantToInt32ArrayAlloc(const VARIANT *const var, LONG **pprgn, ULONG *pcElem);
LONG STDAPICALLTYPE VariantToInt32WithDefault(const VARIANT *const varIn, LONG lDefault);
HRESULT STDAPICALLTYPE VariantToInt64(const VARIANT *const varIn, LONGLONG *pllRet);
HRESULT STDAPICALLTYPE VariantToInt64Array(const VARIANT *const var, LONGLONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE VariantToInt64ArrayAlloc(const VARIANT *const var, LONGLONG **pprgn, ULONG *pcElem);
LONGLONG STDAPICALLTYPE VariantToInt64WithDefault(const VARIANT *const varIn, LONGLONG llDefault);
HRESULT STDAPICALLTYPE VariantToStringAlloc(const VARIANT *const varIn, PWSTR *ppszBuf);
HRESULT STDAPICALLTYPE VariantToStringArray(const VARIANT *const var, PWSTR *prgsz, ULONG crgsz, ULONG *pcElem);
HRESULT STDAPICALLTYPE VariantToStringArrayAlloc(const VARIANT *const var, PWSTR **pprgsz, ULONG *pcElem);
HRESULT STDAPICALLTYPE VariantToStrRet(const VARIANT *const varIn, STRRET *pstrret);
HRESULT STDAPICALLTYPE VariantToUInt16(const VARIANT *const varIn, USHORT *puiRet);
HRESULT STDAPICALLTYPE VariantToUInt16Array(const VARIANT *const var, USHORT *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE VariantToUInt16ArrayAlloc(const VARIANT *const var, USHORT **pprgn, ULONG *pcElem);
USHORT STDAPICALLTYPE VariantToUInt16WithDefault(const VARIANT *const varIn, USHORT uiDefault);
HRESULT STDAPICALLTYPE VariantToUInt32(const VARIANT *const varIn, ULONG *pulRet);
HRESULT STDAPICALLTYPE VariantToUInt32Array(const VARIANT *const var, ULONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE VariantToUInt32ArrayAlloc(const VARIANT *const var, ULONG **pprgn, ULONG *pcElem);
ULONG STDAPICALLTYPE VariantToUInt32WithDefault(const VARIANT *const varIn, ULONG ulDefault);
HRESULT STDAPICALLTYPE VariantToUInt64(const VARIANT *const varIn, ULONGLONG *pullRet);
HRESULT STDAPICALLTYPE VariantToUInt64Array(const VARIANT *const var, ULONGLONG *prgn, ULONG crgn, ULONG *pcElem);
HRESULT STDAPICALLTYPE VariantToUInt64ArrayAlloc(const VARIANT *const var, ULONGLONG **pprgn, ULONG *pcElem);
ULONGLONG STDAPICALLTYPE VariantToUInt64WithDefault(const VARIANT *const varIn, ULONGLONG ullDefault);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_PROPVARUTIL_H */
