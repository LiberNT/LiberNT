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
