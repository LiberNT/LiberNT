/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests property descriptions and property description lists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS

#include <apitest.h>
#include <objbase.h>
#include <propsys.h>
#include <propkey.h>
#include <wchar.h>

static void
CheckCanonical(IPropertyDescriptionList *List, UINT Index, LPCWSTR Expected)
{
    IPropertyDescription *Desc = NULL;
    LPWSTR Name = NULL;
    HRESULT hr;

    hr = IPropertyDescriptionList_GetAt(List, Index, &IID_IPropertyDescription, (void **)&Desc);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    ok_hr(IPropertyDescription_GetCanonicalName(Desc, &Name), S_OK);
    ok(Name && !wcscmp(Name, Expected), "item %u is %ls, expected %ls\n", Index, Name, Expected);
    CoTaskMemFree(Name);
    IPropertyDescription_Release(Desc);
}

static UINT
ListCount(LPCWSTR Text, HRESULT *Result)
{
    IPropertyDescriptionList *List = NULL;
    UINT Count = (UINT)-1;

    *Result = PSGetPropertyDescriptionListFromString(Text, &IID_IPropertyDescriptionList, (void **)&List);
    if (SUCCEEDED(*Result) && List)
    {
        IPropertyDescriptionList_GetCount(List, &Count);
        IPropertyDescriptionList_Release(List);
    }
    return Count;
}

static void
TestNames(void)
{
    PROPERTYKEY Key;
    LPWSTR Name = NULL;

    ok_hr(PSGetPropertyKeyFromName(L"System.Size", &Key), S_OK);
    ok(IsEqualPropertyKey(Key, PKEY_Size), "System.Size key mismatch\n");
    ok_hr(PSGetPropertyKeyFromName(L"System.ItemNameDisplay", &Key), S_OK);
    ok(IsEqualPropertyKey(Key, PKEY_ItemNameDisplay), "System.ItemNameDisplay key mismatch\n");
    ok(FAILED(PSGetPropertyKeyFromName(L"System.LiberNT.DoesNotExist", &Key)), "unknown name resolved\n");
    ok_hr(PSGetNameFromPropertyKey(&PKEY_DateModified, &Name), S_OK);
    ok(Name && !wcscmp(Name, L"System.DateModified"), "name %ls\n", Name);
    CoTaskMemFree(Name);
}

static void
TestDescription(void)
{
    IPropertyDescription *Desc = NULL;
    PROPDESC_TYPE_FLAGS Flags = 0;
    PROPDESC_DISPLAYTYPE Display = 0;
    PROPVARIANT Value;
    LPWSTR Text = NULL;
    VARTYPE Type = VT_EMPTY;
    UINT Width = 0;
    HRESULT hr;

    hr = PSGetPropertyDescription(&PKEY_Size, &IID_IPropertyDescription, (void **)&Desc);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    ok_hr(IPropertyDescription_GetCanonicalName(Desc, &Text), S_OK);
    ok(Text && !wcscmp(Text, L"System.Size"), "canonical %ls\n", Text);
    CoTaskMemFree(Text);
    Text = NULL;
    ok_hr(IPropertyDescription_GetPropertyType(Desc, &Type), S_OK);
    ok_int(Type, VT_UI8);
    ok_hr(IPropertyDescription_GetDisplayName(Desc, &Text), S_OK);
    ok(Text && !wcscmp(Text, L"Size"), "display name %ls\n", Text);
    CoTaskMemFree(Text);
    Text = NULL;
    ok_hr(IPropertyDescription_GetDisplayType(Desc, &Display), S_OK);
    ok_int(Display, PDDT_NUMBER);
    ok_hr(IPropertyDescription_GetDefaultColumnWidth(Desc, &Width), S_OK);
    ok(Width > 0, "column width %u\n", Width);
    trace("System.Size column width %u\n", Width);
    ok_hr(IPropertyDescription_GetTypeFlags(Desc, PDTF_MASK_ALL, &Flags), S_OK);
    ok(Flags & PDTF_ISVIEWABLE, "type flags %#x\n", Flags);
    trace("System.Size type flags %#x\n", Flags);

    PropVariantInit(&Value);
    Value.vt = VT_UI8;
    Value.uhVal.QuadPart = 2048;
    hr = IPropertyDescription_FormatForDisplay(Desc, &Value, PDFF_DEFAULT, &Text);
    ok_hr(hr, S_OK);
    ok(Text && *Text, "formatted size is empty\n");
    trace("2048 formats as %ls\n", Text);
    CoTaskMemFree(Text);
    Text = NULL;
    PropVariantClear(&Value);
    IPropertyDescription_Release(Desc);

    hr = PSGetPropertyDescriptionByName(L"System.DateModified", &IID_IPropertyDescription, (void **)&Desc);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        ok_hr(IPropertyDescription_GetPropertyType(Desc, &Type), S_OK);
        ok_int(Type, VT_FILETIME);
        ok_hr(IPropertyDescription_GetDisplayName(Desc, &Text), S_OK);
        ok(Text && !wcscmp(Text, L"Date modified"), "display name %ls\n", Text);
        CoTaskMemFree(Text);
        IPropertyDescription_Release(Desc);
    }

    hr = PSGetPropertyDescription(&PKEY_ItemNameDisplay, &IID_IPropertyDescription, (void **)&Desc);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        Text = NULL;
        ok_hr(IPropertyDescription_GetDisplayName(Desc, &Text), S_OK);
        ok(Text && !wcscmp(Text, L"Name"), "display name %ls\n", Text);
        CoTaskMemFree(Text);
        IPropertyDescription_Release(Desc);
    }
}

static void
TestLists(void)
{
    IPropertyDescriptionList *List = NULL;
    UINT Count = 0;
    HRESULT hr;

    hr = PSGetPropertyDescriptionListFromString(L"prop:System.ItemNameDisplay;System.Size;System.DateModified",
                                                &IID_IPropertyDescriptionList, (void **)&List);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        ok_hr(IPropertyDescriptionList_GetCount(List, &Count), S_OK);
        ok_int(Count, 3);
        CheckCanonical(List, 0, L"System.ItemNameDisplay");
        CheckCanonical(List, 1, L"System.Size");
        CheckCanonical(List, 2, L"System.DateModified");
        IPropertyDescriptionList_Release(List);
    }

    Count = ListCount(L"prop:*System.ItemNameDisplay;~System.Size", &hr);
    ok_hr(hr, S_OK);
    ok_int(Count, 2);
    Count = ListCount(L"prop:", &hr);
    ok_hr(hr, S_OK);
    ok_int(Count, 0);
    Count = ListCount(L"prop:System.LiberNT.DoesNotExist;System.Size", &hr);
    trace("unknown entry: hr %#lx count %d\n", hr, (int)Count);
    Count = ListCount(L"System.Size", &hr);
    trace("missing prefix: hr %#lx count %d\n", hr, (int)Count);
    hr = PSGetPropertyDescriptionListFromString(NULL, &IID_IPropertyDescriptionList, (void **)&List);
    ok(FAILED(hr), "NULL list string: %#lx\n", hr);
}

static IPropertyEnumTypeList *
GetEnumList(REFPROPERTYKEY Key, HRESULT *Result)
{
    IPropertyDescription *Desc = NULL;
    IPropertyEnumTypeList *List = NULL;

    *Result = PSGetPropertyDescription(Key, &IID_IPropertyDescription, (void **)&Desc);
    if (FAILED(*Result))
        return NULL;
    *Result = IPropertyDescription_GetEnumTypeList(Desc, &IID_IPropertyEnumTypeList, (void **)&List);
    IPropertyDescription_Release(Desc);
    return SUCCEEDED(*Result) ? List : NULL;
}

static void
TraceFormat(REFPROPERTYKEY Key, PROPVARIANT *Value, LPCSTR What)
{
    LPWSTR Text = NULL;
    HRESULT hr;

    hr = PSFormatForDisplayAlloc(Key, Value, PDFF_DEFAULT, &Text);
    trace("format %s: hr %#lx text %ls\n", What, hr, Text ? Text : L"(null)");
    CoTaskMemFree(Text);
}

static void
TestEnums(void)
{
    IPropertyEnumTypeList *List;
    IPropertyEnumType *Type = NULL;
    PROPENUMTYPE Kind = PET_RANGEDVALUE;
    PROPVARIANT Value;
    LPWSTR Text = NULL;
    LPWSTR Kinds[2] = {L"document", L"picture"};
    UINT Count = 0, Index = 0;
    HRESULT hr;

    List = GetEnumList(&PKEY_Size, &hr);
    trace("System.Size enum list: %#lx\n", hr);
    if (List)
        IPropertyEnumTypeList_Release(List);

    List = GetEnumList(&PKEY_Kind, &hr);
    ok_hr(hr, S_OK);
    if (List)
    {
        ok_hr(IPropertyEnumTypeList_GetCount(List, &Count), S_OK);
        ok_int(Count, 23);
        hr = IPropertyEnumTypeList_GetAt(List, 3, &IID_IPropertyEnumType, (void **)&Type);
        ok_hr(hr, S_OK);
        if (Type)
        {
            ok_hr(IPropertyEnumType_GetEnumType(Type, &Kind), S_OK);
            ok_int(Kind, PET_DISCRETEVALUE);
            PropVariantInit(&Value);
            ok_hr(IPropertyEnumType_GetValue(Type, &Value), S_OK);
            ok(Value.vt == VT_LPWSTR && !wcscmp(Value.pwszVal, L"document"), "value vt %u\n", Value.vt);
            PropVariantClear(&Value);
            ok_hr(IPropertyEnumType_GetDisplayText(Type, &Text), S_OK);
            ok(Text && !wcscmp(Text, L"Document"), "display %ls\n", Text);
            CoTaskMemFree(Text);
            IPropertyEnumType_Release(Type);
        }
        PropVariantInit(&Value);
        Value.vt = VT_LPWSTR;
        Value.pwszVal = Kinds[1];
        hr = IPropertyEnumTypeList_FindMatchingIndex(List, &Value, &Index);
        ok_hr(hr, S_OK);
        ok_int(Index, 14);
        Value.pwszVal = L"liberntnokind";
        Index = 77;
        hr = IPropertyEnumTypeList_FindMatchingIndex(List, &Value, &Index);
        trace("unknown kind: hr %#lx index %u\n", hr, Index);
        IPropertyEnumTypeList_Release(List);
    }

    List = GetEnumList(&PKEY_Rating, &hr);
    ok_hr(hr, S_OK);
    if (List)
    {
        ULONG Ratings[] = {0, 1, 12, 13, 50, 99, 100, 101};
        UINT i;

        ok_hr(IPropertyEnumTypeList_GetCount(List, &Count), S_OK);
        ok_int(Count, 6);
        PropVariantInit(&Value);
        Value.vt = VT_UI4;
        for (i = 0; i < ARRAYSIZE(Ratings); i++)
        {
            Value.ulVal = Ratings[i];
            Index = 77;
            hr = IPropertyEnumTypeList_FindMatchingIndex(List, &Value, &Index);
            trace("rating %lu: hr %#lx index %u\n", Ratings[i], hr, Index);
        }
        IPropertyEnumTypeList_Release(List);
    }

    PropVariantInit(&Value);
    Value.vt = VT_VECTOR | VT_LPWSTR;
    Value.calpwstr.cElems = 1;
    Value.calpwstr.pElems = Kinds;
    TraceFormat(&PKEY_Kind, &Value, "kind document");
    Value.calpwstr.cElems = 2;
    TraceFormat(&PKEY_Kind, &Value, "kind document picture");
    Value.vt = VT_LPWSTR;
    Value.pwszVal = Kinds[0];
    TraceFormat(&PKEY_Kind, &Value, "kind scalar document");
    Value.vt = VT_UI4;
    Value.ulVal = 50;
    TraceFormat(&PKEY_Rating, &Value, "rating 50");
    Value.ulVal = 0;
    TraceFormat(&PKEY_Rating, &Value, "rating 0");
    Value.ulVal = 2;
    TraceFormat(&PKEY_SharingStatus, &Value, "sharing 2");
}

START_TEST(PropertyDescriptionList)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    TestNames();
    TestDescription();
    TestLists();
    TestEnums();
    CoUninitialize();
}
