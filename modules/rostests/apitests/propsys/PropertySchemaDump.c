/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Lists every registered property description for schema comparison
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS

#include <apitest.h>
#include <objbase.h>
#include <propsys.h>
#include <propvarutil.h>
#include <stdio.h>

static void
PrintText(LPCWSTR Text)
{
    if (!Text)
    {
        printf("-");
        return;
    }
    for (; *Text; Text++)
    {
        if (*Text == L'\t' || *Text == L'\n' || *Text == L'\r')
            printf(" ");
        else if (*Text < 0x80)
            printf("%c", (char)*Text);
        else
            printf("\\u%04x", *Text);
    }
}

static void
PrintValue(const PROPVARIANT *Value)
{
    LPWSTR Text = NULL;

    printf("%u:", Value->vt);
    if (Value->vt != VT_EMPTY && SUCCEEDED(PropVariantToStringAlloc(Value, &Text)))
    {
        PrintText(Text);
        CoTaskMemFree(Text);
    }
}

static void
DumpDefaultText(IPropertyDescription *Desc, IPropertyEnumTypeList *List, LPCWSTR Canonical)
{
    static const LONGLONG Probes[] = {0, -2, 7, 1000003, 2147483646, 4294967294LL};
    VARTYPE Type = VT_EMPTY;
    PROPVARIANT Value;
    LPWSTR Text = NULL;
    UINT Index, i;
    HRESULT hr;

    IPropertyDescription_GetPropertyType(Desc, &Type);
    for (i = 0; i < ARRAYSIZE(Probes) + 1; i++)
    {
        PropVariantInit(&Value);
        if ((Type & ~VT_VECTOR) == VT_LPWSTR)
        {
            if (i)
                break;
            Value.vt = VT_LPWSTR;
            Value.pwszVal = L"liberntprobe";
        }
        else if (i == ARRAYSIZE(Probes))
        {
            break;
        }
        else
        {
            PROPVARIANT Number;
            PropVariantInit(&Number);
            Number.vt = VT_I8;
            Number.hVal.QuadPart = Probes[i];
            if (FAILED(PropVariantChangeType(&Value, &Number, 0, Type & ~VT_VECTOR)))
                continue;
        }
        if (SUCCEEDED(IPropertyEnumTypeList_FindMatchingIndex(List, &Value, &Index)))
        {
            if ((Type & ~VT_VECTOR) != VT_LPWSTR)
                PropVariantClear(&Value);
            continue;
        }
        hr = IPropertyDescription_FormatForDisplay(Desc, &Value, PDFF_DEFAULT, &Text);
        printf("DEFAULT\t");
        PrintText(Canonical);
        printf("\t");
        PrintValue(&Value);
        printf("\t%#lx\t", hr);
        PrintText(SUCCEEDED(hr) ? Text : NULL);
        printf("\n");
        CoTaskMemFree(Text);
        Text = NULL;
        if ((Type & ~VT_VECTOR) != VT_LPWSTR)
            PropVariantClear(&Value);
        return;
    }
    printf("DEFAULT\t");
    PrintText(Canonical);
    printf("\t-\t-\t-\n");
}

static void
DumpEnumTypes(IPropertyDescription *Desc, LPCWSTR Canonical)
{
    IPropertyEnumTypeList *List = NULL;
    UINT Count = 0, i;

    if (FAILED(IPropertyDescription_GetEnumTypeList(Desc, &IID_IPropertyEnumTypeList, (void **)&List)))
        return;
    IPropertyEnumTypeList_GetCount(List, &Count);
    if (Count)
        DumpDefaultText(Desc, List, Canonical);
    for (i = 0; i < Count; i++)
    {
        IPropertyEnumType *Type = NULL;
        IPropertyEnumType2 *Type2 = NULL;
        PROPENUMTYPE Kind = PET_DISCRETEVALUE;
        PROPVARIANT Value, Min, Set;
        LPWSTR Display = NULL, Image = NULL;

        if (FAILED(IPropertyEnumTypeList_GetAt(List, i, &IID_IPropertyEnumType, (void **)&Type)))
            continue;
        PropVariantInit(&Value);
        PropVariantInit(&Min);
        PropVariantInit(&Set);
        IPropertyEnumType_GetEnumType(Type, &Kind);
        IPropertyEnumType_GetValue(Type, &Value);
        IPropertyEnumType_GetRangeMinValue(Type, &Min);
        IPropertyEnumType_GetRangeSetValue(Type, &Set);
        if (FAILED(IPropertyEnumType_GetDisplayText(Type, &Display)))
            Display = NULL;
        if (SUCCEEDED(IPropertyEnumType_QueryInterface(Type, &IID_IPropertyEnumType2, (void **)&Type2)))
        {
            if (FAILED(IPropertyEnumType2_GetImageReference(Type2, &Image)))
                Image = NULL;
            IPropertyEnumType2_Release(Type2);
        }
        printf("ENUM\t");
        PrintText(Canonical);
        printf("\t%u\t%u\t", i, Kind);
        PrintValue(&Value);
        printf("\t");
        PrintValue(&Min);
        printf("\t");
        PrintValue(&Set);
        printf("\t");
        PrintText(Display);
        printf("\t");
        PrintText(Image);
        printf("\n");
        PropVariantClear(&Value);
        PropVariantClear(&Min);
        PropVariantClear(&Set);
        CoTaskMemFree(Display);
        CoTaskMemFree(Image);
        IPropertyEnumType_Release(Type);
    }
    IPropertyEnumTypeList_Release(List);
}

static void
DumpDescription(IPropertyDescription *Desc)
{
    PROPERTYKEY Key = {0};
    LPWSTR Canonical = NULL, Display = NULL, Invitation = NULL;
    VARTYPE Type = VT_EMPTY;
    PROPDESC_TYPE_FLAGS TypeFlags = 0;
    PROPDESC_VIEW_FLAGS ViewFlags = 0;
    UINT Width = 0;
    PROPDESC_DISPLAYTYPE DisplayType = 0;
    SHCOLSTATEF ColumnState = 0;
    PROPDESC_GROUPING_RANGE Grouping = 0;
    PROPDESC_RELATIVEDESCRIPTION_TYPE Relative = 0;
    PROPDESC_SORTDESCRIPTION Sort = 0;
    PROPDESC_AGGREGATION_TYPE Aggregation = 0;
    PROPDESC_CONDITION_TYPE Condition = 0;
    CONDITION_OPERATION Operation = 0;
    WCHAR Guid[40];

    IPropertyDescription_GetPropertyKey(Desc, &Key);
    IPropertyDescription_GetCanonicalName(Desc, &Canonical);
    IPropertyDescription_GetPropertyType(Desc, &Type);
    if (FAILED(IPropertyDescription_GetDisplayName(Desc, &Display)))
        Display = NULL;
    if (FAILED(IPropertyDescription_GetEditInvitation(Desc, &Invitation)))
        Invitation = NULL;
    IPropertyDescription_GetTypeFlags(Desc, PDTF_MASK_ALL, &TypeFlags);
    IPropertyDescription_GetViewFlags(Desc, &ViewFlags);
    IPropertyDescription_GetDefaultColumnWidth(Desc, &Width);
    IPropertyDescription_GetDisplayType(Desc, &DisplayType);
    IPropertyDescription_GetColumnState(Desc, &ColumnState);
    IPropertyDescription_GetGroupingRange(Desc, &Grouping);
    IPropertyDescription_GetRelativeDescriptionType(Desc, &Relative);
    IPropertyDescription_GetSortDescription(Desc, &Sort);
    IPropertyDescription_GetAggregationType(Desc, &Aggregation);
    IPropertyDescription_GetConditionType(Desc, &Condition, &Operation);
    StringFromGUID2(&Key.fmtid, Guid, _countof(Guid));

    printf("PROP\t");
    PrintText(Canonical);
    printf("\t");
    PrintText(Guid);
    printf("\t%lu\t%u\t%#lx\t%#lx\t%u\t%u\t%#lx\t%u\t%u\t%u\t%u\t%u\t%u\t",
           Key.pid, Type, (ULONG)TypeFlags, (ULONG)ViewFlags, Width, DisplayType, (ULONG)ColumnState,
           Grouping, Relative, Sort, Aggregation, Condition, Operation);
    PrintText(Display);
    printf("\t");
    PrintText(Invitation);
    printf("\n");
    DumpEnumTypes(Desc, Canonical);
    CoTaskMemFree(Canonical);
    CoTaskMemFree(Display);
    CoTaskMemFree(Invitation);
}

START_TEST(PropertySchemaDump)
{
    IPropertySystem *System = NULL;
    IPropertyDescriptionList *List = NULL;
    UINT Count = 0, i;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = PSGetPropertySystem(&IID_IPropertySystem, (void **)&System);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        hr = IPropertySystem_EnumeratePropertyDescriptions(System, PDEF_ALL, &IID_IPropertyDescriptionList, (void **)&List);
        ok_hr(hr, S_OK);
        if (SUCCEEDED(hr))
        {
            IPropertyDescriptionList_GetCount(List, &Count);
            trace("%u property descriptions\n", Count);
            for (i = 0; i < Count; i++)
            {
                IPropertyDescription *Desc = NULL;
                if (SUCCEEDED(IPropertyDescriptionList_GetAt(List, i, &IID_IPropertyDescription, (void **)&Desc)))
                {
                    DumpDescription(Desc);
                    IPropertyDescription_Release(Desc);
                }
            }
            IPropertyDescriptionList_Release(List);
        }
        IPropertySystem_Release(System);
    }
    fflush(stdout);
    CoUninitialize();
}
