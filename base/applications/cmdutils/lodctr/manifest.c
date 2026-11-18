/*
 * PROJECT:     LiberNT lodctr
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Performance counter manifests (lodctr /m, unlodctr /m)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winerror.h>
#include <winperf.h>
#include <perflib.h>
#include <objbase.h>
#include <shlwapi.h>
#include <xmllite.h>
#include "manifest.h"

#define PERFLIB_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Perflib"
#define PROVIDERS_KEY PERFLIB_KEY L"\\_V2Providers"
#define COUNTERS_NAMESPACE L"http://schemas.microsoft.com/win/2005/12/counters"
#define PERFLIB_BASE_INDEX 0x737
#define NO_COUNTER ((ULONG)~0UL)

/* One record of a counter set's CounterBlock value, laid out as Windows 11 lodctr writes it */
typedef struct _PERF_REG_COUNTER
{
    ULONG CounterId;
    ULONG Type;
    ULONGLONG Attrib;
    ULONG DetailLevel;
    LONG DefaultScale;
    ULONG NameResource;
    ULONG ExplainResource;
    ULONG AggregateFunc;
    ULONG BaseCounterId;
    ULONG PerfTimeId;
    ULONG PerfFreqId;
    ULONG MultiId;
    ULONG Reserved;
} PERF_REG_COUNTER;

C_ASSERT(sizeof(PERF_REG_COUNTER) == 56);

typedef struct _MAN_COUNTER
{
    PERF_REG_COUNTER Reg;
    BOOL IdGiven;
    BOOL TypeGiven;
    BOOL AggregateGiven;
} MAN_COUNTER;

typedef struct _MAN_SET
{
    GUID Guid;
    BOOL GuidGiven;
    WCHAR *Name;
    ULONG InstanceType;
    ULONG Count;
    MAN_COUNTER *Counters;
} MAN_SET;

typedef struct _MAN_PROVIDER
{
    GUID Guid;
    BOOL GuidGiven;
    WCHAR *Name;
    WCHAR *Application;
    ULONG Type;
    ULONG ResourceBase;
    ULONG SetCount;
    MAN_SET *Sets;
} MAN_PROVIDER;

typedef struct _MANIFEST
{
    ULONG Count;
    MAN_PROVIDER *Providers;
} MANIFEST;

typedef struct _NAME_VALUE
{
    const WCHAR *Name;
    ULONG Value;
} NAME_VALUE;

static const NAME_VALUE CounterTypes[] =
{
    { L"perf_counter_counter", PERF_COUNTER_COUNTER },
    { L"perf_counter_timer", PERF_COUNTER_TIMER },
    { L"perf_counter_queuelen_type", PERF_COUNTER_QUEUELEN_TYPE },
    { L"perf_counter_large_queuelen_type", PERF_COUNTER_LARGE_QUEUELEN_TYPE },
    { L"perf_counter_100ns_queuelen_type", PERF_COUNTER_100NS_QUEUELEN_TYPE },
    { L"perf_counter_obj_time_queuelen_type", PERF_COUNTER_OBJ_TIME_QUEUELEN_TYPE },
    { L"perf_counter_bulk_count", PERF_COUNTER_BULK_COUNT },
    { L"perf_counter_text", PERF_COUNTER_TEXT },
    { L"perf_counter_rawcount", PERF_COUNTER_RAWCOUNT },
    { L"perf_counter_large_rawcount", PERF_COUNTER_LARGE_RAWCOUNT },
    { L"perf_counter_rawcount_hex", PERF_COUNTER_RAWCOUNT_HEX },
    { L"perf_counter_large_rawcount_hex", PERF_COUNTER_LARGE_RAWCOUNT_HEX },
    { L"perf_sample_fraction", PERF_SAMPLE_FRACTION },
    { L"perf_sample_counter", PERF_SAMPLE_COUNTER },
    { L"perf_counter_nodata", PERF_COUNTER_NODATA },
    { L"perf_counter_timer_inv", PERF_COUNTER_TIMER_INV },
    { L"perf_sample_base", PERF_SAMPLE_BASE },
    { L"perf_average_timer", PERF_AVERAGE_TIMER },
    { L"perf_average_base", PERF_AVERAGE_BASE },
    { L"perf_average_bulk", PERF_AVERAGE_BULK },
    { L"perf_obj_time_timer", PERF_OBJ_TIME_TIMER },
    { L"perf_100nsec_timer", PERF_100NSEC_TIMER },
    { L"perf_100nsec_timer_inv", PERF_100NSEC_TIMER_INV },
    { L"perf_counter_multi_timer", PERF_COUNTER_MULTI_TIMER },
    { L"perf_counter_multi_timer_inv", PERF_COUNTER_MULTI_TIMER_INV },
    { L"perf_counter_multi_base", PERF_COUNTER_MULTI_BASE },
    { L"perf_100nsec_multi_timer", PERF_100NSEC_MULTI_TIMER },
    { L"perf_100nsec_multi_timer_inv", PERF_100NSEC_MULTI_TIMER_INV },
    { L"perf_raw_fraction", PERF_RAW_FRACTION },
    { L"perf_large_raw_fraction", PERF_LARGE_RAW_FRACTION },
    { L"perf_raw_base", PERF_RAW_BASE },
    { L"perf_large_raw_base", PERF_LARGE_RAW_BASE },
    { L"perf_elapsed_time", PERF_ELAPSED_TIME },
    { L"perf_counter_delta", PERF_COUNTER_DELTA },
    { L"perf_counter_large_delta", PERF_COUNTER_LARGE_DELTA },
    { L"perf_precision_system_timer", PERF_PRECISION_SYSTEM_TIMER },
    { L"perf_precision_100ns_timer", PERF_PRECISION_100NS_TIMER },
    { L"perf_precision_object_timer", PERF_PRECISION_OBJECT_TIMER },
};

static const NAME_VALUE InstanceTypes[] =
{
    { L"single", PERF_COUNTERSET_SINGLE_INSTANCE },
    { L"multiple", PERF_COUNTERSET_MULTI_INSTANCES },
    { L"globalAggregate", PERF_COUNTERSET_SINGLE_AGGREGATE },
    { L"multipleAggregate", PERF_COUNTERSET_MULTI_AGGREGATE },
    { L"globalAggregateHistory", PERF_COUNTERSET_SINGLE_AGGREGATE_HISTORY },
    { L"instanceAggregate", PERF_COUNTERSET_INSTANCE_AGGREGATE },
};

static const NAME_VALUE Aggregates[] =
{
    { L"undefined", PERF_AGGREGATE_UNDEFINED },
    { L"sum", PERF_AGGREGATE_TOTAL },
    { L"avg", PERF_AGGREGATE_AVG },
    { L"min", PERF_AGGREGATE_MIN },
    { L"max", PERF_AGGREGATE_MAX },
};

static const NAME_VALUE DetailLevels[] =
{
    { L"standard", PERF_DETAIL_NOVICE },
    { L"advanced", PERF_DETAIL_ADVANCED },
};

static const NAME_VALUE ProviderTypes[] =
{
    { L"userMode", 0 },
    { L"kernelMode", 1 },
};

static const NAME_VALUE CounterAttributes[] =
{
    { L"reference", PERF_ATTRIB_BY_REFERENCE },
    { L"noDisplay", PERF_ATTRIB_NO_DISPLAYABLE },
    { L"noDigitGrouping", PERF_ATTRIB_NO_GROUP_SEPARATOR },
    { L"displayAsReal", PERF_ATTRIB_DISPLAY_AS_REAL },
    { L"displayAsHex", PERF_ATTRIB_DISPLAY_AS_HEX },
};

static BOOL
LookupName(const NAME_VALUE *Table, SIZE_T Count, const WCHAR *Name, ULONG *Value)
{
    SIZE_T Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (!wcscmp(Table[Index].Name, Name))
        {
            *Value = Table[Index].Value;
            return TRUE;
        }
    }
    return FALSE;
}

static const WCHAR *
LookupValue(const NAME_VALUE *Table, SIZE_T Count, ULONG Value)
{
    SIZE_T Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Table[Index].Value == Value)
            return Table[Index].Name;
    }
    return L"?";
}

static WCHAR *
DupString(const WCHAR *Text)
{
    SIZE_T Size = (wcslen(Text) + 1) * sizeof(WCHAR);
    WCHAR *Copy = malloc(Size);

    if (Copy)
        memcpy(Copy, Text, Size);
    return Copy;
}

static BOOL
ParseGuid(const WCHAR *Text, GUID *Guid)
{
    unsigned long Data1;
    unsigned int Data2, Data3, Bytes[8], Index;

    if (wcslen(Text) != 38 || Text[0] != L'{' || Text[37] != L'}')
        return FALSE;
    if (swscanf(Text, L"{%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}", &Data1, &Data2, &Data3, &Bytes[0], &Bytes[1],
                &Bytes[2], &Bytes[3], &Bytes[4], &Bytes[5], &Bytes[6], &Bytes[7]) != 11)
        return FALSE;
    Guid->Data1 = Data1;
    Guid->Data2 = (USHORT)Data2;
    Guid->Data3 = (USHORT)Data3;
    for (Index = 0; Index < 8; Index++)
        Guid->Data4[Index] = (BYTE)Bytes[Index];
    return TRUE;
}

static void
FormatGuid(const GUID *Guid, WCHAR *Text, SIZE_T Size)
{
    swprintf(Text, Size, L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", (unsigned long)Guid->Data1,
             Guid->Data2, Guid->Data3, Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3],
             Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
}

static BOOL
ParseUlong(const WCHAR *Text, ULONG *Value)
{
    WCHAR *End;
    unsigned long Parsed;

    if (!*Text)
        return FALSE;
    errno = 0;
    Parsed = wcstoul(Text, &End, 10);
    if (*End || errno == ERANGE)
        return FALSE;
    *Value = (ULONG)Parsed;
    return TRUE;
}

static BOOL
ParseLong(const WCHAR *Text, LONG *Value)
{
    WCHAR *End;
    long Parsed;

    if (!*Text)
        return FALSE;
    errno = 0;
    Parsed = wcstol(Text, &End, 10);
    if (*End || errno == ERANGE)
        return FALSE;
    *Value = (LONG)Parsed;
    return TRUE;
}

static void
FreeManifest(MANIFEST *Manifest)
{
    ULONG Provider, Set;

    for (Provider = 0; Provider < Manifest->Count; Provider++)
    {
        MAN_PROVIDER *Entry = &Manifest->Providers[Provider];

        for (Set = 0; Set < Entry->SetCount; Set++)
        {
            free(Entry->Sets[Set].Name);
            free(Entry->Sets[Set].Counters);
        }
        free(Entry->Sets);
        free(Entry->Name);
        free(Entry->Application);
    }
    free(Manifest->Providers);
    Manifest->Providers = NULL;
    Manifest->Count = 0;
}

static void *
GrowArray(void **Array, ULONG *Count, SIZE_T ElementSize)
{
    BYTE *Grown = realloc(*Array, (*Count + 1) * ElementSize);

    if (!Grown)
        return NULL;
    *Array = Grown;
    memset(Grown + *Count * ElementSize, 0, ElementSize);
    return Grown + (*Count)++ * ElementSize;
}

static void
InvalidEnumWarning(IXmlReader *Reader, const WCHAR *Value, const WCHAR *Attribute, const WCHAR *Element,
                   const WCHAR *Default)
{
    UINT Line = 0, Column = 0;

    IXmlReader_GetLineNumber(Reader, &Line);
    IXmlReader_GetLinePosition(Reader, &Column);
    printf("Warning: Invalid value \"%ls\" for attribute \"%ls\" in element <%ls>, using default \"%ls\" instead. "
           "(line=%u, column=%u)\n", Value, Attribute, Element, Default, Line, Column);
}

static HRESULT
InvalidValueError(IXmlReader *Reader, const WCHAR *Value, const WCHAR *Attribute, const WCHAR *Element)
{
    UINT Line = 0, Column = 0;

    IXmlReader_GetLineNumber(Reader, &Line);
    IXmlReader_GetLinePosition(Reader, &Column);
    printf("Error: Invalid value \"%ls\" for attribute \"%ls\" in element <%ls>. (line=%u, column=%u)\n", Value,
           Attribute, Element, Line, Column);
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

static HRESULT
ReadProviderAttribute(IXmlReader *Reader, MAN_PROVIDER *Provider, const WCHAR *Name, const WCHAR *Value)
{
    if (!wcscmp(Name, L"providerGuid"))
    {
        if (!ParseGuid(Value, &Provider->Guid))
            return InvalidValueError(Reader, Value, Name, L"provider");
        Provider->GuidGiven = TRUE;
    }
    else if (!wcscmp(Name, L"providerName"))
    {
        free(Provider->Name);
        if (!(Provider->Name = DupString(Value)))
            return E_OUTOFMEMORY;
    }
    else if (!wcscmp(Name, L"applicationIdentity"))
    {
        free(Provider->Application);
        if (!(Provider->Application = DupString(Value)))
            return E_OUTOFMEMORY;
    }
    else if (!wcscmp(Name, L"providerType"))
    {
        if (!LookupName(ProviderTypes, ARRAYSIZE(ProviderTypes), Value, &Provider->Type))
        {
            InvalidEnumWarning(Reader, Value, Name, L"provider", L"userMode");
            Provider->Type = 0;
        }
    }
    else if (!wcscmp(Name, L"resourceBase"))
    {
        if (!ParseUlong(Value, &Provider->ResourceBase))
            return InvalidValueError(Reader, Value, Name, L"provider");
    }
    return S_OK;
}

static HRESULT
ReadSetAttribute(IXmlReader *Reader, MAN_SET *Set, const WCHAR *Name, const WCHAR *Value)
{
    if (!wcscmp(Name, L"guid"))
    {
        if (!ParseGuid(Value, &Set->Guid))
            return InvalidValueError(Reader, Value, Name, L"counterSet");
        Set->GuidGiven = TRUE;
    }
    else if (!wcscmp(Name, L"name"))
    {
        free(Set->Name);
        if (!(Set->Name = DupString(Value)))
            return E_OUTOFMEMORY;
    }
    else if (!wcscmp(Name, L"instances"))
    {
        if (!LookupName(InstanceTypes, ARRAYSIZE(InstanceTypes), Value, &Set->InstanceType))
            return InvalidValueError(Reader, Value, Name, L"counterSet");
    }
    return S_OK;
}

static HRESULT
ReadCounterAttribute(IXmlReader *Reader, MAN_COUNTER *Counter, const WCHAR *Name, const WCHAR *Value)
{
    PERF_REG_COUNTER *Reg = &Counter->Reg;
    ULONG *Partner = NULL;

    if (!wcscmp(Name, L"id"))
    {
        if (!ParseUlong(Value, &Reg->CounterId))
            return InvalidValueError(Reader, Value, Name, L"counter");
        Counter->IdGiven = TRUE;
    }
    else if (!wcscmp(Name, L"type"))
    {
        if (!LookupName(CounterTypes, ARRAYSIZE(CounterTypes), Value, &Reg->Type))
            return InvalidValueError(Reader, Value, Name, L"counter");
        Counter->TypeGiven = TRUE;
    }
    else if (!wcscmp(Name, L"detailLevel"))
    {
        if (!LookupName(DetailLevels, ARRAYSIZE(DetailLevels), Value, &Reg->DetailLevel))
            return InvalidValueError(Reader, Value, Name, L"counter");
    }
    else if (!wcscmp(Name, L"defaultScale"))
    {
        if (!ParseLong(Value, &Reg->DefaultScale))
            return InvalidValueError(Reader, Value, Name, L"counter");
    }
    else if (!wcscmp(Name, L"aggregate"))
    {
        if (!LookupName(Aggregates, ARRAYSIZE(Aggregates), Value, &Reg->AggregateFunc))
        {
            InvalidEnumWarning(Reader, Value, Name, L"counter", L"sum");
            Reg->AggregateFunc = PERF_AGGREGATE_TOTAL;
        }
        Counter->AggregateGiven = TRUE;
    }
    else if (!wcscmp(Name, L"baseID"))
        Partner = &Reg->BaseCounterId;
    else if (!wcscmp(Name, L"perfTimeID"))
        Partner = &Reg->PerfTimeId;
    else if (!wcscmp(Name, L"perfFreqID"))
        Partner = &Reg->PerfFreqId;
    else if (!wcscmp(Name, L"multiCounterID"))
        Partner = &Reg->MultiId;

    if (Partner && !ParseUlong(Value, Partner))
        return InvalidValueError(Reader, Value, Name, L"counter");
    return S_OK;
}

static HRESULT
ReadElement(IXmlReader *Reader, const WCHAR *Element, MAN_PROVIDER *Provider, MAN_SET *Set, MAN_COUNTER *Counter)
{
    HRESULT hr = IXmlReader_MoveToFirstAttribute(Reader);

    while (hr == S_OK)
    {
        const WCHAR *Name, *Value;

        if (FAILED(hr = IXmlReader_GetLocalName(Reader, &Name, NULL)) ||
            FAILED(hr = IXmlReader_GetValue(Reader, &Value, NULL)))
            break;
        if (!wcscmp(Element, L"provider"))
            hr = ReadProviderAttribute(Reader, Provider, Name, Value);
        else if (!wcscmp(Element, L"counterSet"))
            hr = ReadSetAttribute(Reader, Set, Name, Value);
        else if (!wcscmp(Element, L"counter"))
            hr = ReadCounterAttribute(Reader, Counter, Name, Value);
        else if (!wcscmp(Name, L"name"))
        {
            ULONG Attribute;

            if (LookupName(CounterAttributes, ARRAYSIZE(CounterAttributes), Value, &Attribute))
                Counter->Reg.Attrib |= Attribute;
            else
                hr = InvalidValueError(Reader, Value, Name, L"counterAttribute");
        }
        if (FAILED(hr))
            break;
        hr = IXmlReader_MoveToNextAttribute(Reader);
    }
    IXmlReader_MoveToElement(Reader);
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT
ParseManifest(IXmlReader *Reader, MANIFEST *Manifest)
{
    MAN_PROVIDER *Provider = NULL;
    MAN_SET *Set = NULL;
    MAN_COUNTER *Counter = NULL;
    XmlNodeType Type;
    HRESULT hr;

    while ((hr = IXmlReader_Read(Reader, &Type)) == S_OK)
    {
        const WCHAR *Uri, *Local;

        if (Type != XmlNodeType_Element)
            continue;
        if (FAILED(hr = IXmlReader_GetNamespaceUri(Reader, &Uri, NULL)) ||
            FAILED(hr = IXmlReader_GetLocalName(Reader, &Local, NULL)))
            break;
        if (wcscmp(Uri, COUNTERS_NAMESPACE))
            continue;

        if (!wcscmp(Local, L"provider"))
        {
            if (!(Provider = GrowArray((void **)&Manifest->Providers, &Manifest->Count, sizeof(*Provider))))
                return E_OUTOFMEMORY;
            Provider->ResourceBase = 1;
            Set = NULL;
            Counter = NULL;
        }
        else if (!wcscmp(Local, L"counterSet") && Provider)
        {
            if (!(Set = GrowArray((void **)&Provider->Sets, &Provider->SetCount, sizeof(*Set))))
                return E_OUTOFMEMORY;
            Counter = NULL;
        }
        else if (!wcscmp(Local, L"counter") && Set)
        {
            if (!(Counter = GrowArray((void **)&Set->Counters, &Set->Count, sizeof(*Counter))))
                return E_OUTOFMEMORY;
            Counter->Reg.DetailLevel = PERF_DETAIL_NOVICE;
            Counter->Reg.BaseCounterId = NO_COUNTER;
            Counter->Reg.PerfTimeId = NO_COUNTER;
            Counter->Reg.PerfFreqId = NO_COUNTER;
            Counter->Reg.MultiId = NO_COUNTER;
        }
        else if (!wcscmp(Local, L"counterAttribute") && Counter)
        {
        }
        else
        {
            continue;
        }
        hr = ReadElement(Reader, Local, Provider, Set, Counter);
        if (FAILED(hr))
            break;
    }
    return FAILED(hr) ? hr : S_OK;
}

static MAN_COUNTER *
FindCounter(MAN_SET *Set, ULONG Id)
{
    ULONG Index;

    for (Index = 0; Index < Set->Count; Index++)
    {
        if (Set->Counters[Index].Reg.CounterId == Id)
            return &Set->Counters[Index];
    }
    return NULL;
}

static BOOL
UsesBase(ULONG Type)
{
    /* TODO: INFO_NEEDED whether precision timers keep their baseID */
    return Type == PERF_SAMPLE_FRACTION || Type == PERF_RAW_FRACTION || Type == PERF_LARGE_RAW_FRACTION ||
           Type == PERF_AVERAGE_TIMER || Type == PERF_AVERAGE_BULK;
}

static BOOL
IsMultiTimer(ULONG Type)
{
    return (Type & PERF_MULTI_COUNTER) && Type != PERF_COUNTER_MULTI_BASE;
}

static HRESULT
CheckSet(MAN_SET *Set)
{
    ULONG Index;

    for (Index = 0; Index < Set->Count; Index++)
    {
        MAN_COUNTER *Counter = &Set->Counters[Index];
        PERF_REG_COUNTER *Reg = &Counter->Reg;
        ULONG *Partners[] = { &Reg->BaseCounterId, &Reg->PerfTimeId, &Reg->PerfFreqId, &Reg->MultiId };
        ULONG Partner;

        if (!Counter->IdGiven || !Counter->TypeGiven)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (!Counter->AggregateGiven)
            Reg->AggregateFunc = (Set->InstanceType & PERF_COUNTERSET_FLAG_AGGREGATE) ? PERF_AGGREGATE_TOTAL :
                                                                                       PERF_AGGREGATE_UNDEFINED;

        /* A counter keeps only the partners its type uses */
        if (!UsesBase(Reg->Type))
            Reg->BaseCounterId = NO_COUNTER;
        if (!(Reg->Type & PERF_OBJECT_TIMER))
            Reg->PerfTimeId = Reg->PerfFreqId = NO_COUNTER;
        if (!IsMultiTimer(Reg->Type))
            Reg->MultiId = NO_COUNTER;
        for (Partner = 0; Partner < ARRAYSIZE(Partners); Partner++)
        {
            if (*Partners[Partner] != NO_COUNTER && !FindCounter(Set, *Partners[Partner]))
                *Partners[Partner] = NO_COUNTER;
        }

        /* TODO: check the base, time and frequency partners other counter types require */
        if (IsMultiTimer(Reg->Type))
        {
            MAN_COUNTER *Multi = Reg->MultiId != NO_COUNTER ? FindCounter(Set, Reg->MultiId) : NULL;

            if (!Multi)
            {
                printf("Error: Cannot find \"multiCounterID\" counter corresponding to counter %lu with type %ls.\n",
                       Reg->CounterId, LookupValue(CounterTypes, ARRAYSIZE(CounterTypes), Reg->Type));
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            if (Multi->Reg.Type != PERF_COUNTER_RAWCOUNT)
            {
                printf("Error: \"multiCounterID\" counter %lu should use type perf_counter_rawcount, not %ls.\n",
                       Multi->Reg.CounterId, LookupValue(CounterTypes, ARRAYSIZE(CounterTypes), Multi->Reg.Type));
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
    }
    return S_OK;
}

static HRESULT
ReadManifest(const WCHAR *Path, MANIFEST *Manifest)
{
    IStream *Stream = NULL;
    IXmlReader *Reader = NULL;
    ULONG Provider, Set;
    HRESULT hr;

    hr = SHCreateStreamOnFileW(Path, STGM_READ | STGM_SHARE_DENY_WRITE, &Stream);
    if (FAILED(hr))
    {
        printf("Error: Cannot open manifest file (error code 0x%08lX).\n", hr);
        return hr;
    }
    hr = CreateXmlReader(&IID_IXmlReader, (void **)&Reader, NULL);
    if (SUCCEEDED(hr))
        hr = IXmlReader_SetProperty(Reader, XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
    if (SUCCEEDED(hr))
        hr = IXmlReader_SetInput(Reader, (IUnknown *)Stream);
    if (SUCCEEDED(hr))
    {
        hr = ParseManifest(Reader, Manifest);
        if (FAILED(hr))
        {
            UINT Line = 0, Column = 0;

            IXmlReader_GetLineNumber(Reader, &Line);
            IXmlReader_GetLinePosition(Reader, &Column);
            if (HRESULT_FACILITY(hr) != FACILITY_WIN32)
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            printf("Error: Failed to parse XML manifest file (error code 0x%08lX). (line=%u, column=%u)\n", hr, Line,
                   Column);
        }
    }
    if (Reader)
        IXmlReader_Release(Reader);
    IStream_Release(Stream);

    for (Provider = 0; SUCCEEDED(hr) && Provider < Manifest->Count; Provider++)
    {
        MAN_PROVIDER *Entry = &Manifest->Providers[Provider];

        if (!Entry->GuidGiven)
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        for (Set = 0; SUCCEEDED(hr) && Set < Entry->SetCount; Set++)
            hr = Entry->Sets[Set].GuidGiven ? CheckSet(&Entry->Sets[Set]) : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return hr;
}

static LONG
SetDword(HKEY Key, const WCHAR *Name, DWORD Value)
{
    return RegSetValueExW(Key, Name, 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
}

static LONG
SetString(HKEY Key, const WCHAR *Name, DWORD Type, const WCHAR *Value)
{
    return RegSetValueExW(Key, Name, 0, Type, (const BYTE *)Value, (DWORD)((wcslen(Value) + 1) * sizeof(WCHAR)));
}

static LONG
WriteSet(HKEY Provider, const MAN_SET *Set, ULONG NameResource, ULONG FirstCounter, const PERF_REG_COUNTER *Block)
{
    WCHAR Guid[40];
    HKEY Key;
    LONG Error;

    FormatGuid(&Set->Guid, Guid, ARRAYSIZE(Guid));
    Error = RegCreateKeyExW(Provider, Guid, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    if (Error)
        return Error;
    if (!(Error = SetDword(Key, L"NameResource", NameResource)) &&
        !(Error = SetDword(Key, L"ExplainResource", NameResource + 2)) &&
        !(Error = SetString(Key, L"NeutralName", REG_SZ, Set->Name ? Set->Name : L"")) &&
        !(Error = SetDword(Key, L"InstanceType", Set->InstanceType)) &&
        !(Error = SetDword(Key, L"First Counter", FirstCounter)) &&
        !(Error = SetDword(Key, L"Last Counter", FirstCounter + 2 * Set->Count)) &&
        !(Error = RegSetValueExW(Key, L"CounterBlock", 0, REG_BINARY, (const BYTE *)Block,
                                 Set->Count * sizeof(*Block))))
        Error = SetDword(Key, L"CounterCount", Set->Count);
    RegCloseKey(Key);
    return Error;
}

/* An installed provider is kept when its application has the file name the manifest gives */
static LONG
CheckInstalledProvider(HKEY Key, const MAN_PROVIDER *Provider)
{
    WCHAR Installed[MAX_PATH];
    DWORD Size = sizeof(Installed);
    const WCHAR *Name;
    LONG Error;

    Error = RegGetValueW(Key, NULL, L"ApplicationIdentity", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, NULL,
                         Installed, &Size);
    if (Error)
        return Error;
    Name = wcsrchr(Installed, L'\\');
    Name = Name ? Name + 1 : Installed;
    return _wcsicmp(Name, Provider->Application ? Provider->Application : L"") ? ERROR_INVALID_DATA :
                                                                                 ERROR_ALREADY_EXISTS;
}

static LONG
InstallProvider(const MAN_PROVIDER *Provider, const WCHAR *Directory, const WCHAR *ProviderKey, ULONG *LastCounter,
                BOOL *Existed)
{
    ULONG Set, Index, Resource = Provider->ResourceBase, Counter = *LastCounter;
    WCHAR *Application = NULL;
    HKEY Key = NULL;
    DWORD Disposition;
    LONG Error;

    *Existed = FALSE;
    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, ProviderKey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Key, &Disposition);
    if (Error)
        return Error;
    *Existed = (Disposition == REG_OPENED_EXISTING_KEY);
    if (*Existed)
        Error = CheckInstalledProvider(Key, Provider);
    else
    {
        const WCHAR *Name = Provider->Application ? Provider->Application : L"";
        SIZE_T Length = wcslen(Directory) + 1 + wcslen(Name) + 1;

        if (!(Application = malloc(Length * sizeof(WCHAR))))
            Error = ERROR_OUTOFMEMORY;
        else
        {
            swprintf(Application, Length, L"%ls\\%ls", Directory, Name);
            if (!(Error = SetDword(Key, L"ProviderType", Provider->Type)) &&
                !(Error = SetString(Key, L"ProviderName", REG_SZ, Provider->Name ? Provider->Name : L"")))
                Error = SetString(Key, L"ApplicationIdentity", REG_EXPAND_SZ, Application);
        }
    }

    /* Counter indexes are taken even when the provider is already installed */
    for (Set = 0; Set < Provider->SetCount; Set++)
    {
        const MAN_SET *Entry = &Provider->Sets[Set];
        PERF_REG_COUNTER *Block = calloc(max(Entry->Count, 1), sizeof(*Block));
        ULONG NameResource = Resource, First = Counter + 2;

        Resource += 4;
        for (Index = 0; Block && Index < Entry->Count; Index++)
        {
            Block[Index] = Entry->Counters[Index].Reg;
            Block[Index].NameResource = Resource;
            Block[Index].ExplainResource = Resource + 2;
            Resource += 4;
        }
        Counter = First + 2 * Entry->Count;
        if (!Error && !Block)
            Error = ERROR_OUTOFMEMORY;
        if (!Error)
            Error = WriteSet(Key, Entry, NameResource, First, Block);
        free(Block);
    }
    *LastCounter = Counter;
    free(Application);
    RegCloseKey(Key);

    if (Error && Error != ERROR_ALREADY_EXISTS && !RegDeleteTreeW(HKEY_LOCAL_MACHINE, ProviderKey))
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, ProviderKey);
    return Error;
}

static DWORD
QueryDword(HKEY Key, const WCHAR *Name, DWORD Default)
{
    DWORD Value, Size = sizeof(Value), Type;

    if (RegQueryValueExW(Key, Name, NULL, &Type, (BYTE *)&Value, &Size) || Type != REG_DWORD || Size != sizeof(Value))
        return Default;
    return Value;
}

static BOOL
ResolveManifest(const WCHAR *Manifest, WCHAR *Full, DWORD Size, WCHAR **FilePart)
{
    DWORD Length;

    if (!*Manifest)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Length = GetFullPathNameW(Manifest, Size, Full, FilePart);
    if (!Length || Length >= Size)
    {
        SetLastError(Length ? ERROR_FILENAME_EXCED_RANGE : GetLastError());
        return FALSE;
    }
    return TRUE;
}

int
LodctrInstallManifest(const WCHAR *Manifest, const WCHAR *Path)
{
    WCHAR Full[MAX_PATH], Directory[MAX_PATH], ProviderKey[128], Guid[40], *FilePart = NULL;
    MANIFEST Parsed = { 0 };
    ULONG Provider, LastCounter;
    HKEY Perflib;
    HRESULT hr, Result = S_OK;
    LONG Error;

    if (!ResolveManifest(Manifest, Full, ARRAYSIZE(Full), &FilePart))
    {
        Error = GetLastError();
        printf("Error: Unable to resolve path for \"/m:%ls\"; error code is %lu.\n", Manifest, (ULONG)Error);
        return Error;
    }
    if (Path)
        wcsncpy(Directory, Path, ARRAYSIZE(Directory) - 1);
    else
        wcsncpy(Directory, Full, ARRAYSIZE(Directory) - 1);
    Directory[ARRAYSIZE(Directory) - 1] = 0;
    if (!Path && FilePart)
        Directory[FilePart - Full - 1] = 0;

    hr = ReadManifest(Full, &Parsed);
    if (SUCCEEDED(hr))
    {
        /* TODO: serialize the counter index allocation with concurrent installers */
        Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, PERFLIB_KEY, 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL,
                                &Perflib, NULL);
        if (Error)
            hr = HRESULT_FROM_WIN32(Error);
    }
    if (FAILED(hr))
    {
        printf("Error: Unable to install the performance counter provider(s) listed in file \"%ls\"; "
               "error code is %lu.\n", Manifest, (ULONG)hr);
        FreeManifest(&Parsed);
        return hr;
    }

    LastCounter = QueryDword(Perflib, L"Last Counter", QueryDword(Perflib, L"Base Index", PERFLIB_BASE_INDEX) - 1);
    for (Provider = 0; Provider < Parsed.Count; Provider++)
    {
        const MAN_PROVIDER *Entry = &Parsed.Providers[Provider];
        BOOL Existed;

        FormatGuid(&Entry->Guid, Guid, ARRAYSIZE(Guid));
        swprintf(ProviderKey, ARRAYSIZE(ProviderKey), L"%ls\\%ls", PROVIDERS_KEY, Guid);
        Error = InstallProvider(Entry, Directory, ProviderKey, &LastCounter, &Existed);
        if (Error == ERROR_ALREADY_EXISTS)
            printf("Info: Unable to install performance counters from file \"%ls\"; provider \"%ls\" is already "
                   "installed.\n", Full, Guid);
        else if (Error)
        {
            if (Existed)
                printf("Error: Unable to query existing provider \"%ls\", error code %lu.\n", Guid, (ULONG)Error);
            Result = HRESULT_FROM_WIN32(Error);
        }
        if (Error)
        {
            printf("Warning: Unable to install provider \"%ls\" for \"%ls\", error code %lu.\n", Guid,
                   Entry->Application ? Entry->Application : L"", (ULONG)Error);
        }
    }
    SetDword(Perflib, L"Last Counter", LastCounter);
    SetDword(Perflib, L"Last Help", LastCounter + 1);
    RegCloseKey(Perflib);
    FreeManifest(&Parsed);

    if (FAILED(Result))
    {
        printf("Error: Unable to install the performance counter provider(s) listed in file \"%ls\"; "
               "error code is %lu.\n", Manifest, (ULONG)Result);
        return Result;
    }
    printf("Info: Successfully installed performance counters from file \"%ls\".\n", Full);
    return 0;
}

int
LodctrUninstallManifest(const WCHAR *Manifest)
{
    WCHAR Full[MAX_PATH], ProviderKey[128], Guid[40];
    MANIFEST Parsed = { 0 };
    ULONG Provider;
    HRESULT hr, Failure = S_OK;
    LONG Error;

    if (!ResolveManifest(Manifest, Full, ARRAYSIZE(Full), NULL))
    {
        Error = GetLastError();
        printf("Error: Unable to resolve path for \"/m:%ls\"; error code is %lu.\n", Manifest, (ULONG)Error);
        return Error;
    }
    hr = ReadManifest(Full, &Parsed);
    if (FAILED(hr))
    {
        printf("Error: Unable to uninstall the performance counter provider(s) listed in file \"%ls\"; "
               "error code is %lu.\n", Manifest, (ULONG)hr);
        FreeManifest(&Parsed);
        return hr;
    }
    for (Provider = 0; Provider < Parsed.Count; Provider++)
    {
        FormatGuid(&Parsed.Providers[Provider].Guid, Guid, ARRAYSIZE(Guid));
        swprintf(ProviderKey, ARRAYSIZE(ProviderKey), L"%ls\\%ls", PROVIDERS_KEY, Guid);
        Error = RegDeleteTreeW(HKEY_LOCAL_MACHINE, ProviderKey);
        if (!Error)
            Error = RegDeleteKeyW(HKEY_LOCAL_MACHINE, ProviderKey);
        if (Error)
        {
            printf("Warning: Unable to uninstall the performance counters for provider \"%ls\"; error code is %lu.\n",
                   Guid, (ULONG)Error);
            Failure = HRESULT_FROM_WIN32(Error);
        }
    }
    FreeManifest(&Parsed);

    if (FAILED(Failure))
        printf("Warning: Unable to uninstall the performance counter provider(s) listed in file \"%ls\"; "
               "error code is %lu.\n", Full, (ULONG)Failure);
    else
        printf("Info: Successfully uninstalled performance counter provider(s) listed in file \"%ls\".\n", Full);
    return 0;
}
