/*
 * PROJECT:     LiberNT Property System
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows property schema, descriptions and description lists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>
#include <shlwapi.h>
#include <propvarutil.h>
#include "propsys_private.h"

typedef struct _PROPSCHEMA_ENTRY
{
    LPCWSTR CanonicalName;
    GUID FormatId;
    DWORD PropertyId;
    VARTYPE Type;
    ULONG TypeFlags;
    ULONG ViewFlags;
    UINT ColumnWidth;
    UINT DisplayType;
    ULONG ColumnState;
    UINT Grouping;
    UINT Relative;
    UINT Sort;
    UINT Aggregation;
    UINT Condition;
    UINT Operation;
    UINT DisplayNameId;
    UINT InvitationId;
    USHORT EnumFirst;
    USHORT EnumCount;
    UINT DefaultTextId;
} PROPSCHEMA_ENTRY;

typedef struct _PROPSCHEMA_ENUM
{
    UCHAR EnumType;
    UCHAR Bounds;
    VARTYPE Type;
    LONGLONG Value;
    LONGLONG Min;
    LONGLONG Set;
    LPCWSTR Text;
    UINT DisplayId;
    LPCWSTR Image;
} PROPSCHEMA_ENUM;

#define PROPSCHEMA_ENUM_HAS_MIN 0x1
#define PROPSCHEMA_ENUM_HAS_SET 0x2

#define PROPSCHEMA_DEFAULT_FAIL 0
#define PROPSCHEMA_DEFAULT_VALUE ((UINT)-1)
#define PROPSCHEMA_DEFAULT_EMPTY ((UINT)-2)
#define PROPSCHEMA_TEXT_EMPTY ((UINT)-2)

#include "propschema_table.h"

static HINSTANCE
SchemaModule(void)
{
    static HINSTANCE Module;
    HMODULE Handle = NULL;

    if (!Module &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)SchemaModule, &Handle))
        Module = Handle;
    return Module;
}

static HRESULT
DupString(LPCWSTR Text, LPWSTR *Out)
{
    SIZE_T Size;

    if (!Out)
        return E_POINTER;
    *Out = NULL;
    if (!Text)
        return E_FAIL;
    Size = (wcslen(Text) + 1) * sizeof(WCHAR);
    if (!(*Out = CoTaskMemAlloc(Size)))
        return E_OUTOFMEMORY;
    memcpy(*Out, Text, Size);
    return S_OK;
}

static HRESULT
LoadResourceString(UINT Id, LPWSTR *Out)
{
    const WCHAR *Text;
    INT Length;

    if (!Out)
        return E_POINTER;
    *Out = NULL;
    if (!Id)
        return E_FAIL;
    Length = LoadStringW(SchemaModule(), Id, (LPWSTR)&Text, 0);
    if (Length <= 0)
        return E_FAIL;
    if (!(*Out = CoTaskMemAlloc((Length + 1) * sizeof(WCHAR))))
        return E_OUTOFMEMORY;
    memcpy(*Out, Text, Length * sizeof(WCHAR));
    (*Out)[Length] = 0;
    return S_OK;
}

static const PROPSCHEMA_ENTRY *
FindByKey(const PROPERTYKEY *Key)
{
    SIZE_T i;

    for (i = 0; i < ARRAY_SIZE(PropertySchema); i++)
    {
        if (PropertySchema[i].PropertyId == Key->pid && IsEqualGUID(&PropertySchema[i].FormatId, &Key->fmtid))
            return &PropertySchema[i];
    }
    return NULL;
}

static const PROPSCHEMA_ENTRY *
FindByName(LPCWSTR Name, SIZE_T Length)
{
    SIZE_T i;

    for (i = 0; i < ARRAY_SIZE(PropertySchema); i++)
    {
        LPCWSTR Canonical = PropertySchema[i].CanonicalName;
        if (wcslen(Canonical) == Length && !_wcsnicmp(Canonical, Name, Length))
            return &PropertySchema[i];
    }
    return NULL;
}

typedef struct _SCHEMA_DESCRIPTION
{
    IPropertyDescription IPropertyDescription_iface;
    LONG Ref;
    const PROPSCHEMA_ENTRY *Entry;
} SCHEMA_DESCRIPTION;

static inline SCHEMA_DESCRIPTION *
impl_from_description(IPropertyDescription *iface)
{
    return CONTAINING_RECORD(iface, SCHEMA_DESCRIPTION, IPropertyDescription_iface);
}

static HRESULT WINAPI
desc_QueryInterface(IPropertyDescription *iface, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyDescription))
    {
        *ppv = iface;
        IPropertyDescription_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
desc_AddRef(IPropertyDescription *iface)
{
    return InterlockedIncrement(&impl_from_description(iface)->Ref);
}

static ULONG WINAPI
desc_Release(IPropertyDescription *iface)
{
    SCHEMA_DESCRIPTION *This = impl_from_description(iface);
    ULONG Ref = InterlockedDecrement(&This->Ref);

    if (!Ref)
        free(This);
    return Ref;
}

static HRESULT WINAPI
desc_GetPropertyKey(IPropertyDescription *iface, PROPERTYKEY *pkey)
{
    const PROPSCHEMA_ENTRY *Entry = impl_from_description(iface)->Entry;

    if (!pkey)
        return E_POINTER;
    pkey->fmtid = Entry->FormatId;
    pkey->pid = Entry->PropertyId;
    return S_OK;
}

static HRESULT WINAPI
desc_GetCanonicalName(IPropertyDescription *iface, LPWSTR *name)
{
    return DupString(impl_from_description(iface)->Entry->CanonicalName, name);
}

static HRESULT WINAPI
desc_GetPropertyType(IPropertyDescription *iface, VARTYPE *vt)
{
    if (!vt)
        return E_POINTER;
    *vt = impl_from_description(iface)->Entry->Type;
    return S_OK;
}

static HRESULT WINAPI
desc_GetDisplayName(IPropertyDescription *iface, LPWSTR *name)
{
    return LoadResourceString(impl_from_description(iface)->Entry->DisplayNameId, name);
}

static HRESULT WINAPI
desc_GetEditInvitation(IPropertyDescription *iface, LPWSTR *invite)
{
    return LoadResourceString(impl_from_description(iface)->Entry->InvitationId, invite);
}

static HRESULT WINAPI
desc_GetTypeFlags(IPropertyDescription *iface, PROPDESC_TYPE_FLAGS mask, PROPDESC_TYPE_FLAGS *flags)
{
    if (!flags)
        return E_POINTER;
    *flags = impl_from_description(iface)->Entry->TypeFlags & mask;
    return S_OK;
}

static HRESULT WINAPI
desc_GetViewFlags(IPropertyDescription *iface, PROPDESC_VIEW_FLAGS *flags)
{
    if (!flags)
        return E_POINTER;
    *flags = impl_from_description(iface)->Entry->ViewFlags;
    return S_OK;
}

static HRESULT WINAPI
desc_GetDefaultColumnWidth(IPropertyDescription *iface, UINT *chars)
{
    if (!chars)
        return E_POINTER;
    *chars = impl_from_description(iface)->Entry->ColumnWidth;
    return S_OK;
}

static HRESULT WINAPI
desc_GetDisplayType(IPropertyDescription *iface, PROPDESC_DISPLAYTYPE *type)
{
    if (!type)
        return E_POINTER;
    *type = impl_from_description(iface)->Entry->DisplayType;
    return S_OK;
}

static HRESULT WINAPI
desc_GetColumnState(IPropertyDescription *iface, SHCOLSTATEF *flags)
{
    if (!flags)
        return E_POINTER;
    *flags = impl_from_description(iface)->Entry->ColumnState;
    return S_OK;
}

static HRESULT WINAPI
desc_GetGroupingRange(IPropertyDescription *iface, PROPDESC_GROUPING_RANGE *range)
{
    if (!range)
        return E_POINTER;
    *range = impl_from_description(iface)->Entry->Grouping;
    return S_OK;
}

static HRESULT WINAPI
desc_GetRelativeDescriptionType(IPropertyDescription *iface, PROPDESC_RELATIVEDESCRIPTION_TYPE *type)
{
    if (!type)
        return E_POINTER;
    *type = impl_from_description(iface)->Entry->Relative;
    return S_OK;
}

static HRESULT WINAPI
desc_GetRelativeDescription(IPropertyDescription *iface, REFPROPVARIANT var1, REFPROPVARIANT var2,
                            LPWSTR *desc1, LPWSTR *desc2)
{
    HRESULT hr;

    if (!desc1 || !desc2)
        return E_POINTER;
    *desc1 = *desc2 = NULL;
    hr = IPropertyDescription_FormatForDisplay(iface, var1, PDFF_DEFAULT, desc1);
    if (SUCCEEDED(hr))
        hr = IPropertyDescription_FormatForDisplay(iface, var2, PDFF_DEFAULT, desc2);
    if (FAILED(hr))
    {
        CoTaskMemFree(*desc1);
        *desc1 = NULL;
    }
    return hr;
}

static HRESULT WINAPI
desc_GetSortDescription(IPropertyDescription *iface, PROPDESC_SORTDESCRIPTION *psd)
{
    if (!psd)
        return E_POINTER;
    *psd = impl_from_description(iface)->Entry->Sort;
    return S_OK;
}

static HRESULT WINAPI
desc_GetSortDescriptionLabel(IPropertyDescription *iface, BOOL descending, LPWSTR *desc)
{
    if (!desc)
        return E_POINTER;
    *desc = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI
desc_GetAggregationType(IPropertyDescription *iface, PROPDESC_AGGREGATION_TYPE *type)
{
    if (!type)
        return E_POINTER;
    *type = impl_from_description(iface)->Entry->Aggregation;
    return S_OK;
}

static HRESULT WINAPI
desc_GetConditionType(IPropertyDescription *iface, PROPDESC_CONDITION_TYPE *contype, CONDITION_OPERATION *opdefault)
{
    const PROPSCHEMA_ENTRY *Entry = impl_from_description(iface)->Entry;

    if (!contype || !opdefault)
        return E_POINTER;
    *contype = Entry->Condition;
    *opdefault = Entry->Operation;
    return S_OK;
}

static HRESULT
EnumNumberToPropVariant(const PROPSCHEMA_ENUM *Enum, LONGLONG Number, PROPVARIANT *Out)
{
    PropVariantInit(Out);
    switch (Enum->Type)
    {
        case VT_LPWSTR:
            Out->vt = VT_LPWSTR;
            return DupString(Enum->Text, &Out->pwszVal);
        case VT_BOOL:
            Out->vt = VT_BOOL;
            Out->boolVal = Number ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case VT_R8:
            Out->vt = VT_R8;
            Out->dblVal = (double)Number;
            return S_OK;
        case VT_I4:
            Out->vt = VT_I4;
            Out->lVal = (LONG)Number;
            return S_OK;
        case VT_UI1:
            Out->vt = VT_UI1;
            Out->bVal = (BYTE)Number;
            return S_OK;
        case VT_UI2:
            Out->vt = VT_UI2;
            Out->uiVal = (USHORT)Number;
            return S_OK;
        case VT_UI4:
            Out->vt = VT_UI4;
            Out->ulVal = (ULONG)Number;
            return S_OK;
        case VT_UI8:
            Out->vt = VT_UI8;
            Out->uhVal.QuadPart = (ULONGLONG)Number;
            return S_OK;
    }
    return E_UNEXPECTED;
}

static LONGLONG
EnumLowerBound(const PROPSCHEMA_ENUM *Enum)
{
    return (Enum->Bounds & PROPSCHEMA_ENUM_HAS_MIN) ? Enum->Min : Enum->Value;
}

static INT
EnumCompare(const PROPSCHEMA_ENUM *Enum, LONGLONG Number, const PROPVARIANT *Value)
{
    PROPVARIANT Bound;
    INT Result;

    if (FAILED(EnumNumberToPropVariant(Enum, Number, &Bound)))
        return 1;
    Result = PropVariantCompareEx(Value, &Bound, PVCU_DEFAULT, PVCF_USESTRCMPI);
    PropVariantClear(&Bound);
    return Result;
}

static HRESULT
EnumFindMatch(const PROPSCHEMA_ENTRY *Entry, const PROPVARIANT *Value, UINT *Index)
{
    const PROPSCHEMA_ENUM *Enums = &PropertyEnums[Entry->EnumFirst];
    UINT i, Default = (UINT)-1;

    *Index = (UINT)-1;
    for (i = 0; i < Entry->EnumCount; i++)
    {
        const PROPSCHEMA_ENUM *Enum = &Enums[i];

        switch (Enum->EnumType)
        {
            case PET_DISCRETEVALUE:
                if (!EnumCompare(Enum, Enum->Value, Value))
                {
                    *Index = i;
                    return S_OK;
                }
                break;
            case PET_RANGEDVALUE:
                if (EnumCompare(Enum, EnumLowerBound(Enum), Value) >= 0 &&
                    (i + 1 >= Entry->EnumCount || EnumCompare(&Enums[i + 1], EnumLowerBound(&Enums[i + 1]), Value) < 0))
                {
                    *Index = i;
                    return S_OK;
                }
                break;
            case PET_DEFAULTVALUE:
                Default = i;
                break;
        }
    }
    if (Default != (UINT)-1)
    {
        *Index = Default;
        return S_OK;
    }
    return E_FAIL;
}

typedef struct _SCHEMA_ENUMTYPE
{
    IPropertyEnumType2 IPropertyEnumType2_iface;
    LONG Ref;
    const PROPSCHEMA_ENUM *Enum;
} SCHEMA_ENUMTYPE;

static inline SCHEMA_ENUMTYPE *
impl_from_enumtype(IPropertyEnumType2 *iface)
{
    return CONTAINING_RECORD(iface, SCHEMA_ENUMTYPE, IPropertyEnumType2_iface);
}

static HRESULT WINAPI
enumtype_QueryInterface(IPropertyEnumType2 *iface, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyEnumType) ||
        IsEqualIID(riid, &IID_IPropertyEnumType2))
    {
        *ppv = iface;
        IPropertyEnumType2_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
enumtype_AddRef(IPropertyEnumType2 *iface)
{
    return InterlockedIncrement(&impl_from_enumtype(iface)->Ref);
}

static ULONG WINAPI
enumtype_Release(IPropertyEnumType2 *iface)
{
    SCHEMA_ENUMTYPE *This = impl_from_enumtype(iface);
    ULONG Ref = InterlockedDecrement(&This->Ref);

    if (!Ref)
        free(This);
    return Ref;
}

static HRESULT WINAPI
enumtype_GetEnumType(IPropertyEnumType2 *iface, PROPENUMTYPE *penumtype)
{
    if (!penumtype)
        return E_POINTER;
    *penumtype = impl_from_enumtype(iface)->Enum->EnumType;
    return S_OK;
}

static HRESULT WINAPI
enumtype_GetValue(IPropertyEnumType2 *iface, PROPVARIANT *ppropvar)
{
    const PROPSCHEMA_ENUM *Enum = impl_from_enumtype(iface)->Enum;

    if (!ppropvar)
        return E_POINTER;
    return EnumNumberToPropVariant(Enum, Enum->Value, ppropvar);
}

static HRESULT WINAPI
enumtype_GetRangeMinValue(IPropertyEnumType2 *iface, PROPVARIANT *ppropvarMin)
{
    const PROPSCHEMA_ENUM *Enum = impl_from_enumtype(iface)->Enum;

    if (!ppropvarMin)
        return E_POINTER;
    PropVariantInit(ppropvarMin);
    if (!(Enum->Bounds & PROPSCHEMA_ENUM_HAS_MIN))
        return S_OK;
    return EnumNumberToPropVariant(Enum, Enum->Min, ppropvarMin);
}

static HRESULT WINAPI
enumtype_GetRangeSetValue(IPropertyEnumType2 *iface, PROPVARIANT *ppropvarSet)
{
    const PROPSCHEMA_ENUM *Enum = impl_from_enumtype(iface)->Enum;

    if (!ppropvarSet)
        return E_POINTER;
    PropVariantInit(ppropvarSet);
    if (!(Enum->Bounds & PROPSCHEMA_ENUM_HAS_SET))
        return S_OK;
    return EnumNumberToPropVariant(Enum, Enum->Set, ppropvarSet);
}

static HRESULT
LoadEnumText(UINT Id, LPWSTR *Out)
{
    if (Id == PROPSCHEMA_TEXT_EMPTY)
        return DupString(L"", Out);
    return LoadResourceString(Id, Out);
}

static HRESULT WINAPI
enumtype_GetDisplayText(IPropertyEnumType2 *iface, LPWSTR *ppszDisplay)
{
    return LoadEnumText(impl_from_enumtype(iface)->Enum->DisplayId, ppszDisplay);
}

static HRESULT WINAPI
enumtype_GetImageReference(IPropertyEnumType2 *iface, LPWSTR *ppszImageRes)
{
    return DupString(impl_from_enumtype(iface)->Enum->Image, ppszImageRes);
}

static const IPropertyEnumType2Vtbl SchemaEnumTypeVtbl =
{
    enumtype_QueryInterface,
    enumtype_AddRef,
    enumtype_Release,
    enumtype_GetEnumType,
    enumtype_GetValue,
    enumtype_GetRangeMinValue,
    enumtype_GetRangeSetValue,
    enumtype_GetDisplayText,
    enumtype_GetImageReference,
};

typedef struct _SCHEMA_ENUMLIST
{
    IPropertyEnumTypeList IPropertyEnumTypeList_iface;
    LONG Ref;
    const PROPSCHEMA_ENTRY *Entry;
} SCHEMA_ENUMLIST;

static inline SCHEMA_ENUMLIST *
impl_from_enumlist(IPropertyEnumTypeList *iface)
{
    return CONTAINING_RECORD(iface, SCHEMA_ENUMLIST, IPropertyEnumTypeList_iface);
}

static HRESULT WINAPI
enumlist_QueryInterface(IPropertyEnumTypeList *iface, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyEnumTypeList))
    {
        *ppv = iface;
        IPropertyEnumTypeList_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
enumlist_AddRef(IPropertyEnumTypeList *iface)
{
    return InterlockedIncrement(&impl_from_enumlist(iface)->Ref);
}

static ULONG WINAPI
enumlist_Release(IPropertyEnumTypeList *iface)
{
    SCHEMA_ENUMLIST *This = impl_from_enumlist(iface);
    ULONG Ref = InterlockedDecrement(&This->Ref);

    if (!Ref)
        free(This);
    return Ref;
}

static HRESULT WINAPI
enumlist_GetCount(IPropertyEnumTypeList *iface, UINT *pctypes)
{
    if (!pctypes)
        return E_POINTER;
    *pctypes = impl_from_enumlist(iface)->Entry->EnumCount;
    return S_OK;
}

static HRESULT WINAPI
enumlist_GetAt(IPropertyEnumTypeList *iface, UINT itype, REFIID riid, void **ppv)
{
    const PROPSCHEMA_ENTRY *Entry = impl_from_enumlist(iface)->Entry;
    SCHEMA_ENUMTYPE *Type;
    HRESULT hr;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (itype >= Entry->EnumCount)
        return E_INVALIDARG;
    if (!(Type = calloc(1, sizeof(*Type))))
        return E_OUTOFMEMORY;
    Type->IPropertyEnumType2_iface.lpVtbl = &SchemaEnumTypeVtbl;
    Type->Ref = 1;
    Type->Enum = &PropertyEnums[Entry->EnumFirst + itype];
    hr = IPropertyEnumType2_QueryInterface(&Type->IPropertyEnumType2_iface, riid, ppv);
    IPropertyEnumType2_Release(&Type->IPropertyEnumType2_iface);
    return hr;
}

static HRESULT WINAPI
enumlist_GetConditionAt(IPropertyEnumTypeList *iface, UINT nIndex, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI
enumlist_FindMatchingIndex(IPropertyEnumTypeList *iface, REFPROPVARIANT propvarCmp, UINT *pnIndex)
{
    if (!propvarCmp || !pnIndex)
        return E_POINTER;
    return EnumFindMatch(impl_from_enumlist(iface)->Entry, propvarCmp, pnIndex);
}

static const IPropertyEnumTypeListVtbl SchemaEnumListVtbl =
{
    enumlist_QueryInterface,
    enumlist_AddRef,
    enumlist_Release,
    enumlist_GetCount,
    enumlist_GetAt,
    enumlist_GetConditionAt,
    enumlist_FindMatchingIndex,
};

static HRESULT WINAPI
desc_GetEnumTypeList(IPropertyDescription *iface, REFIID riid, void **ppv)
{
    SCHEMA_ENUMLIST *List;
    HRESULT hr;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (!(List = calloc(1, sizeof(*List))))
        return E_OUTOFMEMORY;
    List->IPropertyEnumTypeList_iface.lpVtbl = &SchemaEnumListVtbl;
    List->Ref = 1;
    List->Entry = impl_from_description(iface)->Entry;
    hr = IPropertyEnumTypeList_QueryInterface(&List->IPropertyEnumTypeList_iface, riid, ppv);
    IPropertyEnumTypeList_Release(&List->IPropertyEnumTypeList_iface);
    return hr;
}

static HRESULT WINAPI
desc_CoerceToCanonicalValue(IPropertyDescription *iface, PROPVARIANT *propvar)
{
    VARTYPE Type = impl_from_description(iface)->Entry->Type;

    if (!propvar)
        return E_POINTER;
    if (propvar->vt == VT_EMPTY || propvar->vt == Type)
        return S_OK;
    return PropVariantChangeType(propvar, propvar, 0, Type);
}

static HRESULT
FormatFileTime(const FILETIME *Time, PROPDESC_FORMAT_FLAGS Flags, LPWSTR *Out)
{
    WCHAR Date[64] = L"", Clock[64] = L"", Text[160];
    FILETIME Local;
    SYSTEMTIME System;

    if (!FileTimeToLocalFileTime(Time, &Local) || !FileTimeToSystemTime(&Local, &System))
        return E_FAIL;
    if (!(Flags & PDFF_HIDEDATE))
        GetDateFormatW(LOCALE_USER_DEFAULT, (Flags & PDFF_LONGDATE) ? DATE_LONGDATE : DATE_SHORTDATE,
                       &System, NULL, Date, ARRAY_SIZE(Date));
    if (!(Flags & PDFF_HIDETIME))
        GetTimeFormatW(LOCALE_USER_DEFAULT, (Flags & PDFF_LONGTIME) ? 0 : TIME_NOSECONDS,
                       &System, NULL, Clock, ARRAY_SIZE(Clock));
    if (*Date && *Clock)
        swprintf(Text, ARRAY_SIZE(Text), L"%s %s", Date, Clock);
    else
        swprintf(Text, ARRAY_SIZE(Text), L"%s", *Date ? Date : Clock);
    return DupString(Text, Out);
}

static HRESULT
FormatEnumValue(const PROPSCHEMA_ENTRY *Entry, const PROPVARIANT *Value, LPWSTR *Out)
{
    UINT Index;

    if (SUCCEEDED(EnumFindMatch(Entry, Value, &Index)) &&
        PropertyEnums[Entry->EnumFirst + Index].DisplayId)
        return LoadEnumText(PropertyEnums[Entry->EnumFirst + Index].DisplayId, Out);
    switch (Entry->DefaultTextId)
    {
        case PROPSCHEMA_DEFAULT_FAIL:
            return E_FAIL;
        case PROPSCHEMA_DEFAULT_VALUE:
            return PropVariantToStringAlloc(Value, Out);
        case PROPSCHEMA_DEFAULT_EMPTY:
            return DupString(L"", Out);
    }
    return LoadResourceString(Entry->DefaultTextId, Out);
}

static HRESULT
FormatEnumerated(const PROPSCHEMA_ENTRY *Entry, const PROPVARIANT *Value, LPWSTR *Out)
{
    LPWSTR Text = NULL, Joined = NULL;
    SIZE_T Length = 0, Add;
    ULONG i;
    HRESULT hr;

    if (Value->vt != (VT_VECTOR | VT_LPWSTR))
        return FormatEnumValue(Entry, Value, Out);
    for (i = 0; i < Value->calpwstr.cElems; i++)
    {
        PROPVARIANT Element;
        LPWSTR Grown;

        PropVariantInit(&Element);
        Element.vt = VT_LPWSTR;
        Element.pwszVal = Value->calpwstr.pElems[i];
        hr = FormatEnumValue(Entry, &Element, &Text);
        if (FAILED(hr))
        {
            CoTaskMemFree(Joined);
            return hr;
        }
        Add = wcslen(Text) + (i ? 2 : 0);
        if (!(Grown = CoTaskMemRealloc(Joined, (Length + Add + 1) * sizeof(WCHAR))))
        {
            CoTaskMemFree(Text);
            CoTaskMemFree(Joined);
            return E_OUTOFMEMORY;
        }
        Joined = Grown;
        if (i)
            memcpy(Joined + Length, L"; ", 2 * sizeof(WCHAR));
        memcpy(Joined + Length + (i ? 2 : 0), Text, (wcslen(Text) + 1) * sizeof(WCHAR));
        Length += Add;
        CoTaskMemFree(Text);
        Text = NULL;
    }
    if (!Joined)
        return DupString(L"", Out);
    *Out = Joined;
    return S_OK;
}

static HRESULT WINAPI
desc_FormatForDisplay(IPropertyDescription *iface, REFPROPVARIANT propvar, PROPDESC_FORMAT_FLAGS flags, LPWSTR *display)
{
    const PROPSCHEMA_ENTRY *Entry = impl_from_description(iface)->Entry;
    WCHAR Buffer[64];

    if (!display)
        return E_POINTER;
    *display = NULL;
    if (!propvar)
        return E_INVALIDARG;
    if (propvar->vt == VT_EMPTY)
        return DupString(L"", display);
    if (Entry->DisplayType == PDDT_ENUMERATED && Entry->EnumCount)
        return FormatEnumerated(Entry, propvar, display);
    if (propvar->vt == VT_FILETIME)
        return FormatFileTime(&propvar->filetime, flags, display);
    if (!wcscmp(Entry->CanonicalName, L"System.Size"))
    {
        ULONGLONG Size;
        if (SUCCEEDED(PropVariantToUInt64(propvar, &Size)) && StrFormatByteSizeW((LONGLONG)Size, Buffer, ARRAY_SIZE(Buffer)))
            return DupString(Buffer, display);
    }
    return PropVariantToStringAlloc(propvar, display);
}

static HRESULT WINAPI
desc_IsValueCanonical(IPropertyDescription *iface, REFPROPVARIANT propvar)
{
    if (!propvar)
        return E_INVALIDARG;
    return (propvar->vt == VT_EMPTY || propvar->vt == impl_from_description(iface)->Entry->Type) ? S_OK : S_FALSE;
}

static const IPropertyDescriptionVtbl SchemaDescriptionVtbl =
{
    desc_QueryInterface,
    desc_AddRef,
    desc_Release,
    desc_GetPropertyKey,
    desc_GetCanonicalName,
    desc_GetPropertyType,
    desc_GetDisplayName,
    desc_GetEditInvitation,
    desc_GetTypeFlags,
    desc_GetViewFlags,
    desc_GetDefaultColumnWidth,
    desc_GetDisplayType,
    desc_GetColumnState,
    desc_GetGroupingRange,
    desc_GetRelativeDescriptionType,
    desc_GetRelativeDescription,
    desc_GetSortDescription,
    desc_GetSortDescriptionLabel,
    desc_GetAggregationType,
    desc_GetConditionType,
    desc_GetEnumTypeList,
    desc_CoerceToCanonicalValue,
    desc_FormatForDisplay,
    desc_IsValueCanonical,
};

static HRESULT
CreateDescription(const PROPSCHEMA_ENTRY *Entry, REFIID riid, void **ppv)
{
    SCHEMA_DESCRIPTION *This;
    HRESULT hr;

    if (!(This = calloc(1, sizeof(*This))))
        return E_OUTOFMEMORY;
    This->IPropertyDescription_iface.lpVtbl = &SchemaDescriptionVtbl;
    This->Ref = 1;
    This->Entry = Entry;
    hr = IPropertyDescription_QueryInterface(&This->IPropertyDescription_iface, riid, ppv);
    IPropertyDescription_Release(&This->IPropertyDescription_iface);
    return hr;
}

HRESULT
propschema_get_by_key(const PROPERTYKEY *Key, REFIID riid, void **ppv)
{
    const PROPSCHEMA_ENTRY *Entry;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (!Key)
        return E_POINTER;
    if (!(Entry = FindByKey(Key)))
        return TYPE_E_ELEMENTNOTFOUND;
    return CreateDescription(Entry, riid, ppv);
}

HRESULT
propschema_get_by_name(LPCWSTR Name, REFIID riid, void **ppv)
{
    const PROPSCHEMA_ENTRY *Entry;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (!Name)
        return E_POINTER;
    if (!(Entry = FindByName(Name, wcslen(Name))))
        return TYPE_E_ELEMENTNOTFOUND;
    return CreateDescription(Entry, riid, ppv);
}

typedef struct _SCHEMA_LIST
{
    IPropertyDescriptionList IPropertyDescriptionList_iface;
    LONG Ref;
    UINT Count;
    const PROPSCHEMA_ENTRY **Entries;
} SCHEMA_LIST;

static inline SCHEMA_LIST *
impl_from_list(IPropertyDescriptionList *iface)
{
    return CONTAINING_RECORD(iface, SCHEMA_LIST, IPropertyDescriptionList_iface);
}

static HRESULT WINAPI
list_QueryInterface(IPropertyDescriptionList *iface, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyDescriptionList))
    {
        *ppv = iface;
        IPropertyDescriptionList_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
list_AddRef(IPropertyDescriptionList *iface)
{
    return InterlockedIncrement(&impl_from_list(iface)->Ref);
}

static ULONG WINAPI
list_Release(IPropertyDescriptionList *iface)
{
    SCHEMA_LIST *This = impl_from_list(iface);
    ULONG Ref = InterlockedDecrement(&This->Ref);

    if (!Ref)
    {
        free(This->Entries);
        free(This);
    }
    return Ref;
}

static HRESULT WINAPI
list_GetCount(IPropertyDescriptionList *iface, UINT *pcElem)
{
    if (!pcElem)
        return E_POINTER;
    *pcElem = impl_from_list(iface)->Count;
    return S_OK;
}

static HRESULT WINAPI
list_GetAt(IPropertyDescriptionList *iface, UINT iElem, REFIID riid, void **ppv)
{
    SCHEMA_LIST *This = impl_from_list(iface);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (iElem >= This->Count)
        return E_INVALIDARG;
    return CreateDescription(This->Entries[iElem], riid, ppv);
}

static const IPropertyDescriptionListVtbl SchemaListVtbl =
{
    list_QueryInterface,
    list_AddRef,
    list_Release,
    list_GetCount,
    list_GetAt,
};

static HRESULT
CreateList(const PROPSCHEMA_ENTRY **Entries, UINT Count, REFIID riid, void **ppv)
{
    SCHEMA_LIST *This;
    HRESULT hr;

    if (!(This = calloc(1, sizeof(*This))))
    {
        free(Entries);
        return E_OUTOFMEMORY;
    }
    This->IPropertyDescriptionList_iface.lpVtbl = &SchemaListVtbl;
    This->Ref = 1;
    This->Count = Count;
    This->Entries = Entries;
    hr = IPropertyDescriptionList_QueryInterface(&This->IPropertyDescriptionList_iface, riid, ppv);
    IPropertyDescriptionList_Release(&This->IPropertyDescriptionList_iface);
    return hr;
}

HRESULT
propschema_list_from_string(LPCWSTR Text, REFIID riid, void **ppv)
{
    const PROPSCHEMA_ENTRY **Entries;
    UINT Count = 0, Capacity = 0;
    BOOL Missing = FALSE;
    LPCWSTR p;
    HRESULT hr;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (!Text)
        return E_INVALIDARG;
    if (_wcsnicmp(Text, L"prop:", 5))
        return E_FAIL;
    for (p = Text + 5; *p; p++)
        if (*p == L';')
            Capacity++;
    Capacity++;
    if (!(Entries = calloc(Capacity, sizeof(*Entries))))
        return E_OUTOFMEMORY;
    for (p = Text + 5; *p; )
    {
        LPCWSTR End = wcschr(p, L';');
        SIZE_T Length = End ? (SIZE_T)(End - p) : wcslen(p);
        LPCWSTR Name = p;
        const PROPSCHEMA_ENTRY *Entry;

        while (Length && (*Name == L'*' || *Name == L'~' || *Name == L'#' || *Name == L'@' || *Name == L' '))
        {
            Name++;
            Length--;
        }
        if (Length)
        {
            if ((Entry = FindByName(Name, Length)))
                Entries[Count++] = Entry;
            else
                Missing = TRUE;
        }
        if (!End)
            break;
        p = End + 1;
    }
    hr = CreateList(Entries, Count, riid, ppv);
    if (SUCCEEDED(hr) && Missing)
        hr = INPLACE_S_TRUNCATED;
    return hr;
}

HRESULT
propschema_enumerate(PROPDESC_ENUMFILTER Filter, REFIID riid, void **ppv)
{
    const PROPSCHEMA_ENTRY **Entries;
    UINT Count = 0;
    SIZE_T i;

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (!(Entries = calloc(ARRAY_SIZE(PropertySchema), sizeof(*Entries))))
        return E_OUTOFMEMORY;
    for (i = 0; i < ARRAY_SIZE(PropertySchema); i++)
    {
        ULONG Flags = PropertySchema[i].TypeFlags;

        if (Filter == PDEF_SYSTEM && !(Flags & PDTF_ISSYSTEMPROPERTY))
            continue;
        if (Filter == PDEF_NONSYSTEM && (Flags & PDTF_ISSYSTEMPROPERTY))
            continue;
        if (Filter == PDEF_VIEWABLE && !(Flags & PDTF_ISVIEWABLE))
            continue;
        if (Filter == PDEF_QUERYABLE && !(Flags & PDTF_ISQUERYABLE))
            continue;
        if (Filter == PDEF_INFULLTEXTQUERY && !(Flags & PDTF_INCLUDEINFULLTEXTQUERY))
            continue;
        if (Filter == PDEF_COLUMN && !(Flags & PDTF_ISVIEWABLE))
            continue;
        Entries[Count++] = &PropertySchema[i];
    }
    return CreateList(Entries, Count, riid, ppv);
}

HRESULT
propschema_format_for_display_alloc(const PROPERTYKEY *Key, const PROPVARIANT *propvar,
                                    PROPDESC_FORMAT_FLAGS Flags, LPWSTR *Text)
{
    IPropertyDescription *Desc;
    HRESULT hr;

    if (!Text)
        return E_POINTER;
    *Text = NULL;
    if (!Key || !propvar)
        return E_INVALIDARG;
    hr = propschema_get_by_key(Key, &IID_IPropertyDescription, (void **)&Desc);
    if (FAILED(hr))
        return PropVariantToStringAlloc(propvar, Text);
    hr = IPropertyDescription_FormatForDisplay(Desc, propvar, Flags, Text);
    IPropertyDescription_Release(Desc);
    return hr;
}

HRESULT
propschema_format_for_display(const PROPERTYKEY *Key, const PROPVARIANT *propvar,
                              PROPDESC_FORMAT_FLAGS Flags, LPWSTR Dest, DWORD DestLength)
{
    LPWSTR Text = NULL;
    HRESULT hr;
    SIZE_T Length;

    if (!Dest || !DestLength)
        return E_INVALIDARG;
    *Dest = 0;
    hr = propschema_format_for_display_alloc(Key, propvar, Flags, &Text);
    if (FAILED(hr))
        return hr;
    Length = wcslen(Text);
    if (Length >= DestLength)
    {
        memcpy(Dest, Text, (DestLength - 1) * sizeof(WCHAR));
        Dest[DestLength - 1] = 0;
        hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    else
    {
        memcpy(Dest, Text, (Length + 1) * sizeof(WCHAR));
    }
    CoTaskMemFree(Text);
    return hr;
}

HRESULT WINAPI
PSGetPropertyDescriptionByName(LPCWSTR pszCanonicalName, REFIID riid, void **ppv)
{
    IPropertySystem *System;
    HRESULT hr;

    hr = PSGetPropertySystem(&IID_IPropertySystem, (void **)&System);
    if (FAILED(hr))
        return hr;
    hr = IPropertySystem_GetPropertyDescriptionByName(System, pszCanonicalName, riid, ppv);
    IPropertySystem_Release(System);
    return hr;
}

HRESULT WINAPI
PSEnumeratePropertyDescriptions(PROPDESC_ENUMFILTER filterOn, REFIID riid, void **ppv)
{
    return propschema_enumerate(filterOn, riid, ppv);
}

HRESULT WINAPI
PSFormatForDisplay(REFPROPERTYKEY propkey, REFPROPVARIANT propvar, PROPDESC_FORMAT_FLAGS pdfFlags,
                   LPWSTR pwszText, DWORD cchText)
{
    return propschema_format_for_display(propkey, propvar, pdfFlags, pwszText, cchText);
}

HRESULT WINAPI
PSFormatForDisplayAlloc(REFPROPERTYKEY key, REFPROPVARIANT propvar, PROPDESC_FORMAT_FLAGS pdff, PWSTR *ppszDisplay)
{
    return propschema_format_for_display_alloc(key, propvar, pdff, ppszDisplay);
}
