/*
 * PROJECT:     LiberNT WMI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Classes and instances of the WMI repository and their serialized form
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <oleauto.h>
#include <wchar.h>
#include <assert.h>
#include "wmirepo.h"

#define WMI_KEY_FLAVOR (WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE | WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS | \
                        WBEM_FLAVOR_NOT_OVERRIDABLE)
#define WMI_FLAVOR_PROPAGATION (WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE | WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS)

typedef struct _WMI_QUALIFIER
{
    WCHAR *Name;
    VARIANT Value;
    LONG Flavor;
} WMI_QUALIFIER;

typedef struct _WMI_QUALIFIERS
{
    ULONG Count;
    WMI_QUALIFIER *Items;
} WMI_QUALIFIERS;

typedef struct _WMI_PROPERTY
{
    WCHAR *Name;
    CIMTYPE Type;
    VARIANT Value;
    WCHAR *Origin;
    WMI_QUALIFIERS Qualifiers;
} WMI_PROPERTY;

typedef struct _WMI_METHOD
{
    WCHAR *Name;
    WCHAR *Origin;
    IWbemClassObject *In;
    IWbemClassObject *Out;
    WMI_QUALIFIERS Qualifiers;
} WMI_METHOD;

typedef struct _WMI_OBJECT
{
    IWbemClassObject IWbemClassObject_iface;
    LONG Refs;
    LONG Genus;
    WCHAR *Class;
    WCHAR *Super;
    WCHAR *Server;
    WCHAR *Namespace;
    ULONG DerivationCount;
    WCHAR **Derivation;
    WMI_QUALIFIERS Qualifiers;
    ULONG PropertyCount;
    WMI_PROPERTY *Properties;
    ULONG MethodCount;
    WMI_METHOD *Methods;
    BOOL Enumerating;
    LONG EnumFlags;
    ULONG EnumIndex;
    ULONG MethodIndex;
} WMI_OBJECT;

typedef struct _WMI_QUALIFIER_SET
{
    IWbemQualifierSet IWbemQualifierSet_iface;
    LONG Refs;
    WMI_OBJECT *Owner;
    WCHAR *Property;
    WCHAR *Method;
    ULONG EnumIndex;
    LONG EnumFlags;
} WMI_QUALIFIER_SET;

static const IWbemClassObjectVtbl WmiObjectVtbl;
static const IWbemQualifierSetVtbl WmiQualifierSetVtbl;

static const WCHAR *const SystemProperties[] =
{
    L"__GENUS", L"__CLASS", L"__SUPERCLASS", L"__DYNASTY", L"__RELPATH", L"__PROPERTY_COUNT", L"__DERIVATION",
    L"__SERVER", L"__NAMESPACE", L"__PATH"
};

static inline WMI_OBJECT *
WmiObjectFromIface(IWbemClassObject *Iface)
{
    return CONTAINING_RECORD(Iface, WMI_OBJECT, IWbemClassObject_iface);
}

static WCHAR *
WmiStrDup(const WCHAR *Text)
{
    return Text ? _wcsdup(Text) : NULL;
}

/* Buffers */

void
WmiBufferFree(WMI_BUFFER *Buffer)
{
    free(Buffer->Data);
    memset(Buffer, 0, sizeof(*Buffer));
}

void
WmiBufferPut(WMI_BUFFER *Buffer, const void *Data, SIZE_T Size)
{
    if (Buffer->Failed)
        return;
    if (Size > Buffer->Capacity - Buffer->Size)
    {
        SIZE_T Capacity = max(Buffer->Capacity * 2, Buffer->Size + Size + 256);
        BYTE *Grown;

        if (Capacity < Buffer->Size + Size || !(Grown = realloc(Buffer->Data, Capacity)))
        {
            Buffer->Failed = TRUE;
            return;
        }
        Buffer->Data = Grown;
        Buffer->Capacity = Capacity;
    }
    memcpy(Buffer->Data + Buffer->Size, Data, Size);
    Buffer->Size += Size;
}

void
WmiBufferPutUlong(WMI_BUFFER *Buffer, ULONG Value)
{
    WmiBufferPut(Buffer, &Value, sizeof(Value));
}

void
WmiBufferPutString(WMI_BUFFER *Buffer, const WCHAR *Text)
{
    ULONG Length = Text ? (ULONG)wcslen(Text) : ((ULONG)~0UL);

    WmiBufferPutUlong(Buffer, Length);
    if (Text)
        WmiBufferPut(Buffer, Text, Length * sizeof(WCHAR));
}

BOOL
WmiReaderGet(WMI_READER *Reader, void *Data, SIZE_T Size)
{
    if (Reader->Failed || Size > Reader->Size - Reader->Offset)
    {
        Reader->Failed = TRUE;
        return FALSE;
    }
    memcpy(Data, Reader->Data + Reader->Offset, Size);
    Reader->Offset += Size;
    return TRUE;
}

ULONG
WmiReaderGetUlong(WMI_READER *Reader)
{
    ULONG Value = 0;

    WmiReaderGet(Reader, &Value, sizeof(Value));
    return Value;
}

WCHAR *
WmiReaderGetString(WMI_READER *Reader)
{
    ULONG Length = WmiReaderGetUlong(Reader);
    WCHAR *Text;

    if (Reader->Failed || Length == ((ULONG)~0UL))
        return NULL;
    if (Length > (Reader->Size - Reader->Offset) / sizeof(WCHAR))
    {
        Reader->Failed = TRUE;
        return NULL;
    }
    if (!(Text = malloc((Length + 1) * sizeof(WCHAR))))
    {
        Reader->Failed = TRUE;
        return NULL;
    }
    WmiReaderGet(Reader, Text, Length * sizeof(WCHAR));
    Text[Length] = 0;
    return Text;
}

/* Values */

static VARTYPE
WmiVarTypeOf(CIMTYPE Type)
{
    VARTYPE Base;

    switch (Type & ~CIM_FLAG_ARRAY)
    {
    case CIM_SINT8: case CIM_SINT16: case CIM_CHAR16: Base = VT_I2; break;
    case CIM_UINT8: Base = VT_UI1; break;
    case CIM_UINT16: case CIM_SINT32: case CIM_UINT32: Base = VT_I4; break;
    case CIM_REAL32: Base = VT_R4; break;
    case CIM_REAL64: Base = VT_R8; break;
    case CIM_BOOLEAN: Base = VT_BOOL; break;
    case CIM_OBJECT: Base = VT_UNKNOWN; break;
    default: Base = VT_BSTR; break;
    }
    return (Type & CIM_FLAG_ARRAY) ? (VT_ARRAY | Base) : Base;
}

static CIMTYPE
WmiCimTypeOf(const VARIANT *Value)
{
    CIMTYPE Type;

    switch (V_VT(Value) & ~VT_ARRAY)
    {
    case VT_I1: case VT_I2: Type = CIM_SINT16; break;
    case VT_UI1: Type = CIM_UINT8; break;
    case VT_UI2: case VT_I4: case VT_INT: Type = CIM_SINT32; break;
    case VT_UI4: case VT_UINT: Type = CIM_UINT32; break;
    case VT_I8: Type = CIM_SINT64; break;
    case VT_UI8: Type = CIM_UINT64; break;
    case VT_R4: Type = CIM_REAL32; break;
    case VT_R8: Type = CIM_REAL64; break;
    case VT_BOOL: Type = CIM_BOOLEAN; break;
    case VT_UNKNOWN: case VT_DISPATCH: Type = CIM_OBJECT; break;
    default: Type = CIM_STRING; break;
    }
    return (V_VT(Value) & VT_ARRAY) ? (Type | CIM_FLAG_ARRAY) : Type;
}

static const WCHAR *
WmiTypeName(CIMTYPE Type)
{
    switch (Type & ~CIM_FLAG_ARRAY)
    {
    case CIM_SINT8: return L"sint8";
    case CIM_UINT8: return L"uint8";
    case CIM_SINT16: return L"sint16";
    case CIM_UINT16: return L"uint16";
    case CIM_SINT32: return L"sint32";
    case CIM_UINT32: return L"uint32";
    case CIM_SINT64: return L"sint64";
    case CIM_UINT64: return L"uint64";
    case CIM_REAL32: return L"real32";
    case CIM_REAL64: return L"real64";
    case CIM_BOOLEAN: return L"boolean";
    case CIM_STRING: return L"string";
    case CIM_DATETIME: return L"datetime";
    case CIM_REFERENCE: return L"ref";
    case CIM_CHAR16: return L"char16";
    case CIM_OBJECT: return L"object";
    default: return L"string";
    }
}

static HRESULT
WmiCopyVariant(VARIANT *Destination, const VARIANT *Source)
{
    VariantInit(Destination);
    if (!Source)
    {
        V_VT(Destination) = VT_NULL;
        return S_OK;
    }
    if (V_VT(Source) == VT_UNKNOWN && V_UNKNOWN(Source))
    {
        IWbemClassObject *Object;

        if (SUCCEEDED(IUnknown_QueryInterface(V_UNKNOWN(Source), &IID_IWbemClassObject, (void **)&Object)))
        {
            IWbemClassObject *Copy;
            HRESULT hr = IWbemClassObject_Clone(Object, &Copy);

            IWbemClassObject_Release(Object);
            if (FAILED(hr))
                return hr;
            V_VT(Destination) = VT_UNKNOWN;
            V_UNKNOWN(Destination) = (IUnknown *)Copy;
            return S_OK;
        }
    }
    return VariantCopy(Destination, (VARIANT *)Source);
}

/* Converts a value to the representation WMI uses for the CIM type */
static HRESULT
WmiCoerce(const VARIANT *Source, CIMTYPE Type, VARIANT *Destination)
{
    VARTYPE Target = WmiVarTypeOf(Type);
    HRESULT hr;

    VariantInit(Destination);
    if (!Source || V_VT(Source) == VT_NULL || V_VT(Source) == VT_EMPTY)
    {
        V_VT(Destination) = VT_NULL;
        return S_OK;
    }
    if (V_VT(Source) == Target)
        return WmiCopyVariant(Destination, Source);
    if ((Target & VT_ARRAY) || (V_VT(Source) & VT_ARRAY))
    {
        SAFEARRAY *Array;
        LONG Lower, Upper, Index;
        VARTYPE Element = Target & ~VT_ARRAY;

        if (!(Target & VT_ARRAY) || !(V_VT(Source) & VT_ARRAY) || !V_ARRAY(Source))
            return WBEM_E_TYPE_MISMATCH;
        SafeArrayGetLBound(V_ARRAY(Source), 1, &Lower);
        SafeArrayGetUBound(V_ARRAY(Source), 1, &Upper);
        if (!(Array = SafeArrayCreateVector(Element, 0, Upper - Lower + 1)))
            return WBEM_E_OUT_OF_MEMORY;
        for (Index = Lower; Index <= Upper; Index++)
        {
            VARIANT Item, Converted;
            LONG Target = Index - Lower;

            VariantInit(&Item);
            VariantInit(&Converted);
            hr = SafeArrayGetElement(V_ARRAY(Source), &Index, (V_VT(Source) & ~VT_ARRAY) == VT_VARIANT ? (void *)&Item :
                                     (void *)&V_UNION(&Item, lVal));
            if ((V_VT(Source) & ~VT_ARRAY) != VT_VARIANT)
                V_VT(&Item) = V_VT(Source) & ~VT_ARRAY;
            if (SUCCEEDED(hr))
                hr = VariantChangeType(&Converted, &Item, 0, Element);
            if (SUCCEEDED(hr))
                hr = SafeArrayPutElement(Array, &Target, Element == VT_BSTR ? (void *)V_BSTR(&Converted) :
                                         Element == VT_UNKNOWN ? (void *)V_UNKNOWN(&Converted) :
                                         (void *)&V_UNION(&Converted, lVal));
            VariantClear(&Item);
            VariantClear(&Converted);
            if (FAILED(hr))
            {
                SafeArrayDestroy(Array);
                return WBEM_E_TYPE_MISMATCH;
            }
        }
        V_VT(Destination) = Target;
        V_ARRAY(Destination) = Array;
        return S_OK;
    }
    if (Target == VT_UNKNOWN)
        return WBEM_E_TYPE_MISMATCH;
    hr = VariantChangeType(Destination, (VARIANT *)Source, 0, Target);
    return FAILED(hr) ? WBEM_E_TYPE_MISMATCH : S_OK;
}

static void
WmiSerializeVariant(WMI_BUFFER *Buffer, const VARIANT *Value);
static HRESULT
WmiDeserializeVariant(WMI_READER *Reader, VARIANT *Value);

static void
WmiSerializeScalar(WMI_BUFFER *Buffer, VARTYPE Type, const void *Data)
{
    switch (Type)
    {
    case VT_I1: case VT_UI1:
        WmiBufferPut(Buffer, Data, 1);
        break;
    case VT_I2: case VT_UI2: case VT_BOOL:
        WmiBufferPut(Buffer, Data, 2);
        break;
    case VT_I4: case VT_UI4: case VT_R4: case VT_INT: case VT_UINT:
        WmiBufferPut(Buffer, Data, 4);
        break;
    case VT_I8: case VT_UI8: case VT_R8: case VT_DATE:
        WmiBufferPut(Buffer, Data, 8);
        break;
    case VT_BSTR:
        WmiBufferPutString(Buffer, *(BSTR *)Data);
        break;
    case VT_UNKNOWN:
    {
        IUnknown *Unknown = *(IUnknown **)Data;
        IWbemClassObject *Object = NULL;

        if (Unknown)
            IUnknown_QueryInterface(Unknown, &IID_IWbemClassObject, (void **)&Object);
        WmiBufferPutUlong(Buffer, Object != NULL);
        if (Object)
        {
            if (FAILED(WmiObjectSerialize(Object, Buffer)))
                Buffer->Failed = TRUE;
            IWbemClassObject_Release(Object);
        }
        break;
    }
    default:
        Buffer->Failed = TRUE;
        break;
    }
}

static void
WmiSerializeVariant(WMI_BUFFER *Buffer, const VARIANT *Value)
{
    VARTYPE Type = V_VT(Value);

    WmiBufferPutUlong(Buffer, Type);
    if (Type == VT_EMPTY || Type == VT_NULL)
        return;
    if (Type & VT_ARRAY)
    {
        SAFEARRAY *Array = V_ARRAY(Value);
        LONG Lower = 0, Upper = -1, Index;
        BYTE Element[16];

        if (Array)
        {
            SafeArrayGetLBound(Array, 1, &Lower);
            SafeArrayGetUBound(Array, 1, &Upper);
        }
        WmiBufferPutUlong(Buffer, (ULONG)(Upper - Lower + 1));
        for (Index = Lower; Index <= Upper; Index++)
        {
            memset(Element, 0, sizeof(Element));
            if (FAILED(SafeArrayGetElement(Array, &Index, Element)))
            {
                Buffer->Failed = TRUE;
                return;
            }
            WmiSerializeScalar(Buffer, Type & ~VT_ARRAY, Element);
            if ((Type & ~VT_ARRAY) == VT_BSTR)
                SysFreeString(*(BSTR *)Element);
            else if ((Type & ~VT_ARRAY) == VT_UNKNOWN && *(IUnknown **)Element)
                IUnknown_Release(*(IUnknown **)Element);
        }
        return;
    }
    WmiSerializeScalar(Buffer, Type, &V_UNION(Value, lVal));
}

static HRESULT
WmiDeserializeScalar(WMI_READER *Reader, VARTYPE Type, void *Data)
{
    switch (Type)
    {
    case VT_I1: case VT_UI1:
        WmiReaderGet(Reader, Data, 1);
        break;
    case VT_I2: case VT_UI2: case VT_BOOL:
        WmiReaderGet(Reader, Data, 2);
        break;
    case VT_I4: case VT_UI4: case VT_R4: case VT_INT: case VT_UINT:
        WmiReaderGet(Reader, Data, 4);
        break;
    case VT_I8: case VT_UI8: case VT_R8: case VT_DATE:
        WmiReaderGet(Reader, Data, 8);
        break;
    case VT_BSTR:
    {
        WCHAR *Text = WmiReaderGetString(Reader);

        *(BSTR *)Data = Text ? SysAllocString(Text) : NULL;
        free(Text);
        break;
    }
    case VT_UNKNOWN:
    {
        IWbemClassObject *Object = NULL;

        if (WmiReaderGetUlong(Reader) && FAILED(WmiObjectDeserialize(Reader, &Object)))
            Reader->Failed = TRUE;
        *(IUnknown **)Data = (IUnknown *)Object;
        break;
    }
    default:
        Reader->Failed = TRUE;
        break;
    }
    return Reader->Failed ? WBEM_E_INVALID_OBJECT : S_OK;
}

static HRESULT
WmiDeserializeVariant(WMI_READER *Reader, VARIANT *Value)
{
    VARTYPE Type = (VARTYPE)WmiReaderGetUlong(Reader);

    VariantInit(Value);
    if (Reader->Failed)
        return WBEM_E_INVALID_OBJECT;
    if (Type == VT_EMPTY || Type == VT_NULL)
    {
        V_VT(Value) = Type;
        return S_OK;
    }
    if (Type & VT_ARRAY)
    {
        ULONG Count = WmiReaderGetUlong(Reader), Index;
        SAFEARRAY *Array;

        if (Reader->Failed || Count > Reader->Size - Reader->Offset)
            return WBEM_E_INVALID_OBJECT;
        if (!(Array = SafeArrayCreateVector(Type & ~VT_ARRAY, 0, Count)))
            return WBEM_E_OUT_OF_MEMORY;
        for (Index = 0; Index < Count; Index++)
        {
            BYTE Element[16];
            LONG Position = (LONG)Index;

            memset(Element, 0, sizeof(Element));
            if (FAILED(WmiDeserializeScalar(Reader, Type & ~VT_ARRAY, Element)))
            {
                SafeArrayDestroy(Array);
                return WBEM_E_INVALID_OBJECT;
            }
            SafeArrayPutElement(Array, &Position, (Type & ~VT_ARRAY) == VT_BSTR ? (void *)*(BSTR *)Element :
                                (Type & ~VT_ARRAY) == VT_UNKNOWN ? (void *)*(IUnknown **)Element : (void *)Element);
            if ((Type & ~VT_ARRAY) == VT_BSTR)
                SysFreeString(*(BSTR *)Element);
            else if ((Type & ~VT_ARRAY) == VT_UNKNOWN && *(IUnknown **)Element)
                IUnknown_Release(*(IUnknown **)Element);
        }
        V_VT(Value) = Type;
        V_ARRAY(Value) = Array;
        return S_OK;
    }
    V_VT(Value) = Type;
    return WmiDeserializeScalar(Reader, Type, &V_UNION(Value, lVal));
}

/* Qualifier lists */

static void
WmiQualifiersFree(WMI_QUALIFIERS *List)
{
    ULONG Index;

    for (Index = 0; Index < List->Count; Index++)
    {
        free(List->Items[Index].Name);
        VariantClear(&List->Items[Index].Value);
    }
    free(List->Items);
    List->Items = NULL;
    List->Count = 0;
}

static WMI_QUALIFIER *
WmiQualifierFind(const WMI_QUALIFIERS *List, const WCHAR *Name)
{
    ULONG Index;

    for (Index = 0; Index < List->Count; Index++)
        if (!_wcsicmp(List->Items[Index].Name, Name))
            return &List->Items[Index];
    return NULL;
}

static HRESULT
WmiQualifierSet(WMI_QUALIFIERS *List, const WCHAR *Name, const VARIANT *Value, LONG Flavor)
{
    WMI_QUALIFIER *Qualifier = WmiQualifierFind(List, Name);
    VARIANT Copy;
    HRESULT hr = WmiCopyVariant(&Copy, Value);

    if (FAILED(hr))
        return hr;
    if (!_wcsicmp(Name, L"key"))
        Flavor |= WMI_KEY_FLAVOR;
    if (!Qualifier)
    {
        WMI_QUALIFIER *Grown = realloc(List->Items, (List->Count + 1) * sizeof(*Grown));

        if (!Grown || !(Grown[List->Count].Name = _wcsdup(Name)))
        {
            if (Grown)
                List->Items = Grown;
            VariantClear(&Copy);
            return WBEM_E_OUT_OF_MEMORY;
        }
        List->Items = Grown;
        Qualifier = &Grown[List->Count++];
        VariantInit(&Qualifier->Value);
    }
    VariantClear(&Qualifier->Value);
    Qualifier->Value = Copy;
    Qualifier->Flavor = Flavor;
    return S_OK;
}

static HRESULT
WmiQualifiersCopy(WMI_QUALIFIERS *Destination, const WMI_QUALIFIERS *Source, LONG Required)
{
    ULONG Index;
    HRESULT hr;

    for (Index = 0; Index < Source->Count; Index++)
    {
        const WMI_QUALIFIER *Item = &Source->Items[Index];
        LONG Flavor = Item->Flavor;

        if (Required && !(Flavor & Required))
            continue;
        if (Required)
            Flavor = (Flavor & ~WBEM_FLAVOR_MASK_ORIGIN) | WBEM_FLAVOR_ORIGIN_PROPAGATED;
        hr = WmiQualifierSet(Destination, Item->Name, &Item->Value, Flavor);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static void
WmiQualifiersSerialize(WMI_BUFFER *Buffer, const WMI_QUALIFIERS *List)
{
    ULONG Index;

    WmiBufferPutUlong(Buffer, List->Count);
    for (Index = 0; Index < List->Count; Index++)
    {
        WmiBufferPutString(Buffer, List->Items[Index].Name);
        WmiBufferPutUlong(Buffer, (ULONG)List->Items[Index].Flavor);
        WmiSerializeVariant(Buffer, &List->Items[Index].Value);
    }
}

static HRESULT
WmiQualifiersDeserialize(WMI_READER *Reader, WMI_QUALIFIERS *List)
{
    ULONG Count = WmiReaderGetUlong(Reader), Index;

    if (Reader->Failed || Count > Reader->Size - Reader->Offset)
        return WBEM_E_INVALID_OBJECT;
    if (Count && !(List->Items = calloc(Count, sizeof(*List->Items))))
        return WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; Index < Count; Index++)
    {
        WMI_QUALIFIER *Item = &List->Items[Index];

        List->Count = Index + 1;
        VariantInit(&Item->Value);
        Item->Name = WmiReaderGetString(Reader);
        Item->Flavor = (LONG)WmiReaderGetUlong(Reader);
        if (!Item->Name || FAILED(WmiDeserializeVariant(Reader, &Item->Value)))
            return WBEM_E_INVALID_OBJECT;
    }
    return S_OK;
}

/* Objects */

static WMI_OBJECT *
WmiObjectAlloc(LONG Genus)
{
    WMI_OBJECT *Object = calloc(1, sizeof(*Object));

    if (!Object)
        return NULL;
    Object->IWbemClassObject_iface.lpVtbl = &WmiObjectVtbl;
    Object->Refs = 1;
    Object->Genus = Genus;
    return Object;
}

static void
WmiObjectFree(WMI_OBJECT *Object)
{
    ULONG Index;

    for (Index = 0; Index < Object->PropertyCount; Index++)
    {
        WMI_PROPERTY *Property = &Object->Properties[Index];

        free(Property->Name);
        free(Property->Origin);
        VariantClear(&Property->Value);
        WmiQualifiersFree(&Property->Qualifiers);
    }
    for (Index = 0; Index < Object->MethodCount; Index++)
    {
        WMI_METHOD *Method = &Object->Methods[Index];

        free(Method->Name);
        free(Method->Origin);
        if (Method->In)
            IWbemClassObject_Release(Method->In);
        if (Method->Out)
            IWbemClassObject_Release(Method->Out);
        WmiQualifiersFree(&Method->Qualifiers);
    }
    for (Index = 0; Index < Object->DerivationCount; Index++)
        free(Object->Derivation[Index]);
    free(Object->Derivation);
    free(Object->Properties);
    free(Object->Methods);
    WmiQualifiersFree(&Object->Qualifiers);
    free(Object->Class);
    free(Object->Super);
    free(Object->Server);
    free(Object->Namespace);
    free(Object);
}

static WMI_PROPERTY *
WmiPropertyFind(WMI_OBJECT *Object, const WCHAR *Name)
{
    ULONG Index;

    for (Index = 0; Index < Object->PropertyCount; Index++)
        if (!_wcsicmp(Object->Properties[Index].Name, Name))
            return &Object->Properties[Index];
    return NULL;
}

static WMI_METHOD *
WmiMethodFind(WMI_OBJECT *Object, const WCHAR *Name)
{
    ULONG Index;

    for (Index = 0; Index < Object->MethodCount; Index++)
        if (!_wcsicmp(Object->Methods[Index].Name, Name))
            return &Object->Methods[Index];
    return NULL;
}

static BOOL
WmiPropertyIsKey(const WMI_PROPERTY *Property)
{
    WMI_QUALIFIER *Key = WmiQualifierFind(&Property->Qualifiers, L"key");

    return Key && V_VT(&Key->Value) == VT_BOOL && V_BOOL(&Key->Value);
}

static const WCHAR *
WmiOriginOf(WMI_OBJECT *Object, const WMI_PROPERTY *Property)
{
    return Property->Origin ? Property->Origin : Object->Class;
}

static LONG
WmiPropertyFlavor(WMI_OBJECT *Object, const WMI_PROPERTY *Property)
{
    const WCHAR *Origin = WmiOriginOf(Object, Property);

    return (Origin && Object->Class && _wcsicmp(Origin, Object->Class)) ? WBEM_FLAVOR_ORIGIN_PROPAGATED :
                                                                          WBEM_FLAVOR_ORIGIN_LOCAL;
}

static HRESULT
WmiCloneObject(WMI_OBJECT *Source, WMI_OBJECT **Result)
{
    WMI_BUFFER Buffer = { 0 };
    WMI_READER Reader;
    IWbemClassObject *Copy;
    HRESULT hr = WmiObjectSerialize(&Source->IWbemClassObject_iface, &Buffer);

    if (SUCCEEDED(hr))
    {
        Reader.Data = Buffer.Data;
        Reader.Size = Buffer.Size;
        Reader.Offset = 0;
        Reader.Failed = FALSE;
        hr = WmiObjectDeserialize(&Reader, &Copy);
        if (SUCCEEDED(hr))
            *Result = WmiObjectFromIface(Copy);
    }
    WmiBufferFree(&Buffer);
    return hr;
}

static HRESULT
WmiFormatKeyValue(const WMI_PROPERTY *Property, WCHAR **Text)
{
    VARIANT Converted;
    const WCHAR *Source;
    WCHAR *Out;
    SIZE_T Length = 0, Index;
    BOOL Quote;

    *Text = NULL;
    if (V_VT(&Property->Value) == VT_NULL || V_VT(&Property->Value) == VT_EMPTY)
        return WBEM_E_ILLEGAL_NULL;
    if (V_VT(&Property->Value) == VT_BOOL)
    {
        *Text = _wcsdup(V_BOOL(&Property->Value) ? L"TRUE" : L"FALSE");
        return *Text ? S_OK : WBEM_E_OUT_OF_MEMORY;
    }
    VariantInit(&Converted);
    if (FAILED(VariantChangeType(&Converted, (VARIANT *)&Property->Value, 0, VT_BSTR)))
        return WBEM_E_INVALID_PARAMETER;
    Quote = Property->Type == CIM_STRING || Property->Type == CIM_DATETIME || Property->Type == CIM_REFERENCE ||
            Property->Type == CIM_CHAR16;
    Source = V_BSTR(&Converted);
    for (Index = 0; Source[Index]; Index++)
        Length += (Source[Index] == L'"' || Source[Index] == L'\\') ? 2 : 1;
    Out = malloc((Length + 3) * sizeof(WCHAR));
    if (!Out)
    {
        VariantClear(&Converted);
        return WBEM_E_OUT_OF_MEMORY;
    }
    Length = 0;
    if (Quote)
        Out[Length++] = L'"';
    for (Index = 0; Source[Index]; Index++)
    {
        if (Quote && (Source[Index] == L'"' || Source[Index] == L'\\'))
            Out[Length++] = L'\\';
        Out[Length++] = Source[Index];
    }
    if (Quote)
        Out[Length++] = L'"';
    Out[Length] = 0;
    VariantClear(&Converted);
    *Text = Out;
    return S_OK;
}

static int __cdecl
WmiCompareKeys(const void *Left, const void *Right)
{
    return _wcsicmp((*(const WMI_PROPERTY *const *)Left)->Name, (*(const WMI_PROPERTY *const *)Right)->Name);
}

HRESULT
WmiObjectRelativePath(IWbemClassObject *Iface, WCHAR **Path)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WMI_PROPERTY **Keys;
    ULONG Count = 0, Index;
    SIZE_T Length;
    WCHAR *Out;
    HRESULT hr = S_OK;

    *Path = NULL;
    if (!Object->Class)
        return WBEM_E_INVALID_OBJECT;
    if (Object->Genus == WBEM_GENUS_CLASS)
    {
        *Path = _wcsdup(Object->Class);
        return *Path ? S_OK : WBEM_E_OUT_OF_MEMORY;
    }
    if (!(Keys = calloc(max(Object->PropertyCount, 1), sizeof(*Keys))))
        return WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; Index < Object->PropertyCount; Index++)
        if (WmiPropertyIsKey(&Object->Properties[Index]))
            Keys[Count++] = &Object->Properties[Index];
    if (Count == 0)
    {
        VARIANT Singleton;

        free(Keys);
        if (SUCCEEDED(WmiObjectGetClassQualifier(Iface, L"singleton", &Singleton)))
        {
            VariantClear(&Singleton);
            Length = wcslen(Object->Class) + 3;
            if (!(Out = malloc(Length * sizeof(WCHAR))))
                return WBEM_E_OUT_OF_MEMORY;
            swprintf(Out, Length, L"%ls=@", Object->Class);
            *Path = Out;
            return S_OK;
        }
        return WBEM_E_INVALID_OBJECT;
    }
    qsort(Keys, Count, sizeof(*Keys), WmiCompareKeys);
    Length = wcslen(Object->Class) + 1;
    if (!(Out = _wcsdup(Object->Class)))
    {
        free(Keys);
        return WBEM_E_OUT_OF_MEMORY;
    }
    for (Index = 0; Index < Count && SUCCEEDED(hr); Index++)
    {
        WCHAR *Value, *Grown;
        SIZE_T Extra;

        hr = WmiFormatKeyValue(Keys[Index], &Value);
        if (FAILED(hr))
            break;
        Extra = wcslen(Keys[Index]->Name) + wcslen(Value) + 2;
        if (!(Grown = realloc(Out, (Length + Extra) * sizeof(WCHAR))))
        {
            free(Value);
            hr = WBEM_E_OUT_OF_MEMORY;
            break;
        }
        Out = Grown;
        swprintf(Out + Length - 1, Extra + 1, L"%lc%ls=%ls", Index ? L',' : L'.', Keys[Index]->Name, Value);
        Length += Extra;
        free(Value);
    }
    free(Keys);
    if (FAILED(hr))
    {
        free(Out);
        return hr;
    }
    *Path = Out;
    return S_OK;
}

static HRESULT
WmiSystemProperty(WMI_OBJECT *Object, const WCHAR *Name, VARIANT *Value, CIMTYPE *Type)
{
    const WCHAR *Text = NULL;
    WCHAR *Allocated = NULL;
    BOOL Found = TRUE;

    VariantInit(Value);
    V_VT(Value) = VT_NULL;
    *Type = CIM_STRING;
    if (!_wcsicmp(Name, L"__GENUS"))
    {
        *Type = CIM_SINT32;
        V_VT(Value) = VT_I4;
        V_I4(Value) = Object->Genus;
        return S_OK;
    }
    if (!_wcsicmp(Name, L"__PROPERTY_COUNT"))
    {
        *Type = CIM_SINT32;
        V_VT(Value) = VT_I4;
        V_I4(Value) = (LONG)Object->PropertyCount;
        return S_OK;
    }
    if (!_wcsicmp(Name, L"__DERIVATION"))
    {
        SAFEARRAY *Array = SafeArrayCreateVector(VT_BSTR, 0, Object->DerivationCount);
        LONG Index;

        *Type = CIM_STRING | CIM_FLAG_ARRAY;
        if (!Array)
            return WBEM_E_OUT_OF_MEMORY;
        for (Index = 0; Index < (LONG)Object->DerivationCount; Index++)
        {
            BSTR Item = SysAllocString(Object->Derivation[Index]);

            SafeArrayPutElement(Array, &Index, Item);
            SysFreeString(Item);
        }
        V_VT(Value) = VT_ARRAY | VT_BSTR;
        V_ARRAY(Value) = Array;
        return S_OK;
    }
    if (!_wcsicmp(Name, L"__CLASS"))
        Text = Object->Class;
    else if (!_wcsicmp(Name, L"__SUPERCLASS"))
        Text = Object->Super;
    else if (!_wcsicmp(Name, L"__DYNASTY"))
        Text = Object->DerivationCount ? Object->Derivation[Object->DerivationCount - 1] : Object->Class;
    else if (!_wcsicmp(Name, L"__SERVER"))
        Text = Object->Namespace ? Object->Server : NULL;
    else if (!_wcsicmp(Name, L"__NAMESPACE"))
        Text = Object->Namespace;
    else if (!_wcsicmp(Name, L"__RELPATH") || !_wcsicmp(Name, L"__PATH"))
    {
        WCHAR *Relative;

        if (SUCCEEDED(WmiObjectRelativePath(&Object->IWbemClassObject_iface, &Relative)))
        {
            if (!_wcsicmp(Name, L"__RELPATH"))
                Allocated = Relative;
            else
            {
                if (Object->Namespace && Object->Server)
                {
                    SIZE_T Length = wcslen(Object->Server) + wcslen(Object->Namespace) + wcslen(Relative) + 5;

                    if ((Allocated = malloc(Length * sizeof(WCHAR))))
                        swprintf(Allocated, Length, L"\\\\%ls\\%ls:%ls", Object->Server, Object->Namespace, Relative);
                }
                free(Relative);
            }
            Text = Allocated;
        }
    }
    else
        Found = FALSE;
    if (!Found)
        return WBEM_E_NOT_FOUND;
    if (Text)
    {
        V_VT(Value) = VT_BSTR;
        V_BSTR(Value) = SysAllocString(Text);
    }
    free(Allocated);
    return S_OK;
}

BOOL
WmiObjectIsOurs(IWbemClassObject *Object)
{
    return Object && Object->lpVtbl == &WmiObjectVtbl;
}

LONG
WmiObjectGenus(IWbemClassObject *Object)
{
    return WmiObjectFromIface(Object)->Genus;
}

const WCHAR *
WmiObjectClassName(IWbemClassObject *Object)
{
    return WmiObjectFromIface(Object)->Class;
}

const WCHAR *
WmiObjectSuperclass(IWbemClassObject *Object)
{
    return WmiObjectFromIface(Object)->Super;
}

BOOL
WmiObjectDerivesFrom(IWbemClassObject *Iface, const WCHAR *Class)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    ULONG Index;

    if (Object->Class && !_wcsicmp(Object->Class, Class))
        return TRUE;
    for (Index = 0; Index < Object->DerivationCount; Index++)
        if (!_wcsicmp(Object->Derivation[Index], Class))
            return TRUE;
    return FALSE;
}

void
WmiObjectSetLocation(IWbemClassObject *Iface, const WCHAR *Server, const WCHAR *Namespace)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);

    free(Object->Server);
    free(Object->Namespace);
    Object->Server = WmiStrDup(Server);
    Object->Namespace = WmiStrDup(Namespace);
}

HRESULT
WmiObjectGetClassQualifier(IWbemClassObject *Iface, const WCHAR *Name, VARIANT *Value)
{
    WMI_QUALIFIER *Qualifier = WmiQualifierFind(&WmiObjectFromIface(Iface)->Qualifiers, Name);

    VariantInit(Value);
    if (!Qualifier)
        return WBEM_E_NOT_FOUND;
    return WmiCopyVariant(Value, &Qualifier->Value);
}

HRESULT
WmiObjectCreateClass(IWbemClassObject **Result)
{
    WMI_OBJECT *Object = WmiObjectAlloc(WBEM_GENUS_CLASS);

    if (!Object)
        return WBEM_E_OUT_OF_MEMORY;
    *Result = &Object->IWbemClassObject_iface;
    return S_OK;
}

HRESULT
WmiInstanceFromClass(IWbemClassObject *Class, IWbemClassObject **Instance)
{
    return IWbemClassObject_SpawnInstance(Class, 0, Instance);
}

/* Serialized form: magic, genus, names, derivation, qualifiers, properties, methods */

#define WMI_OBJECT_MAGIC 0x4F494D57

HRESULT
WmiObjectSerialize(IWbemClassObject *Iface, WMI_BUFFER *Buffer)
{
    WMI_OBJECT *Object;
    ULONG Index;

    if (!WmiObjectIsOurs(Iface))
    {
        /* TODO: serialize objects of other implementations through their public interface */
        return WBEM_E_INVALID_OBJECT;
    }
    Object = WmiObjectFromIface(Iface);
    WmiBufferPutUlong(Buffer, WMI_OBJECT_MAGIC);
    WmiBufferPutUlong(Buffer, (ULONG)Object->Genus);
    WmiBufferPutString(Buffer, Object->Class);
    WmiBufferPutString(Buffer, Object->Super);
    WmiBufferPutString(Buffer, Object->Server);
    WmiBufferPutString(Buffer, Object->Namespace);
    WmiBufferPutUlong(Buffer, Object->DerivationCount);
    for (Index = 0; Index < Object->DerivationCount; Index++)
        WmiBufferPutString(Buffer, Object->Derivation[Index]);
    WmiQualifiersSerialize(Buffer, &Object->Qualifiers);
    WmiBufferPutUlong(Buffer, Object->PropertyCount);
    for (Index = 0; Index < Object->PropertyCount; Index++)
    {
        WMI_PROPERTY *Property = &Object->Properties[Index];

        WmiBufferPutString(Buffer, Property->Name);
        WmiBufferPutUlong(Buffer, Property->Type);
        WmiBufferPutString(Buffer, Property->Origin);
        WmiSerializeVariant(Buffer, &Property->Value);
        WmiQualifiersSerialize(Buffer, &Property->Qualifiers);
    }
    WmiBufferPutUlong(Buffer, Object->MethodCount);
    for (Index = 0; Index < Object->MethodCount; Index++)
    {
        WMI_METHOD *Method = &Object->Methods[Index];

        WmiBufferPutString(Buffer, Method->Name);
        WmiBufferPutString(Buffer, Method->Origin);
        WmiQualifiersSerialize(Buffer, &Method->Qualifiers);
        WmiBufferPutUlong(Buffer, Method->In != NULL);
        if (Method->In && FAILED(WmiObjectSerialize(Method->In, Buffer)))
            Buffer->Failed = TRUE;
        WmiBufferPutUlong(Buffer, Method->Out != NULL);
        if (Method->Out && FAILED(WmiObjectSerialize(Method->Out, Buffer)))
            Buffer->Failed = TRUE;
    }
    return Buffer->Failed ? WBEM_E_OUT_OF_MEMORY : S_OK;
}

HRESULT
WmiObjectDeserialize(WMI_READER *Reader, IWbemClassObject **Result)
{
    WMI_OBJECT *Object;
    ULONG Index, Count;
    HRESULT hr = S_OK;

    *Result = NULL;
    if (WmiReaderGetUlong(Reader) != WMI_OBJECT_MAGIC || Reader->Failed)
        return WBEM_E_INVALID_OBJECT;
    if (!(Object = WmiObjectAlloc((LONG)WmiReaderGetUlong(Reader))))
        return WBEM_E_OUT_OF_MEMORY;
    Object->Class = WmiReaderGetString(Reader);
    Object->Super = WmiReaderGetString(Reader);
    Object->Server = WmiReaderGetString(Reader);
    Object->Namespace = WmiReaderGetString(Reader);
    Count = WmiReaderGetUlong(Reader);
    if (Reader->Failed || Count > Reader->Size - Reader->Offset)
        hr = WBEM_E_INVALID_OBJECT;
    else if (Count && !(Object->Derivation = calloc(Count, sizeof(WCHAR *))))
        hr = WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; SUCCEEDED(hr) && Index < Count; Index++)
    {
        Object->DerivationCount = Index + 1;
        if (!(Object->Derivation[Index] = WmiReaderGetString(Reader)))
            hr = WBEM_E_INVALID_OBJECT;
    }
    if (SUCCEEDED(hr))
        hr = WmiQualifiersDeserialize(Reader, &Object->Qualifiers);
    if (SUCCEEDED(hr))
    {
        Count = WmiReaderGetUlong(Reader);
        if (Reader->Failed || Count > Reader->Size - Reader->Offset)
            hr = WBEM_E_INVALID_OBJECT;
        else if (Count && !(Object->Properties = calloc(Count, sizeof(*Object->Properties))))
            hr = WBEM_E_OUT_OF_MEMORY;
        for (Index = 0; SUCCEEDED(hr) && Index < Count; Index++)
        {
            WMI_PROPERTY *Property = &Object->Properties[Index];

            Object->PropertyCount = Index + 1;
            VariantInit(&Property->Value);
            Property->Name = WmiReaderGetString(Reader);
            Property->Type = WmiReaderGetUlong(Reader);
            Property->Origin = WmiReaderGetString(Reader);
            if (!Property->Name)
                hr = WBEM_E_INVALID_OBJECT;
            if (SUCCEEDED(hr))
                hr = WmiDeserializeVariant(Reader, &Property->Value);
            if (SUCCEEDED(hr))
                hr = WmiQualifiersDeserialize(Reader, &Property->Qualifiers);
        }
    }
    if (SUCCEEDED(hr))
    {
        Count = WmiReaderGetUlong(Reader);
        if (Reader->Failed || Count > Reader->Size - Reader->Offset)
            hr = WBEM_E_INVALID_OBJECT;
        else if (Count && !(Object->Methods = calloc(Count, sizeof(*Object->Methods))))
            hr = WBEM_E_OUT_OF_MEMORY;
        for (Index = 0; SUCCEEDED(hr) && Index < Count; Index++)
        {
            WMI_METHOD *Method = &Object->Methods[Index];

            Object->MethodCount = Index + 1;
            Method->Name = WmiReaderGetString(Reader);
            Method->Origin = WmiReaderGetString(Reader);
            if (!Method->Name)
                hr = WBEM_E_INVALID_OBJECT;
            if (SUCCEEDED(hr))
                hr = WmiQualifiersDeserialize(Reader, &Method->Qualifiers);
            if (SUCCEEDED(hr) && WmiReaderGetUlong(Reader))
                hr = WmiObjectDeserialize(Reader, &Method->In);
            if (SUCCEEDED(hr) && WmiReaderGetUlong(Reader))
                hr = WmiObjectDeserialize(Reader, &Method->Out);
        }
    }
    if (SUCCEEDED(hr) && Reader->Failed)
        hr = WBEM_E_INVALID_OBJECT;
    if (FAILED(hr))
    {
        WmiObjectFree(Object);
        return hr;
    }
    *Result = &Object->IWbemClassObject_iface;
    return S_OK;
}

/* IWbemClassObject */

static HRESULT WINAPI
WmiObject_QueryInterface(IWbemClassObject *Iface, REFIID Iid, void **Result)
{
    if (!Result)
        return E_POINTER;
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IWbemClassObject))
    {
        *Result = Iface;
        IWbemClassObject_AddRef(Iface);
        return S_OK;
    }
    *Result = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
WmiObject_AddRef(IWbemClassObject *Iface)
{
    return InterlockedIncrement(&WmiObjectFromIface(Iface)->Refs);
}

static ULONG WINAPI
WmiObject_Release(IWbemClassObject *Iface)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    LONG Refs = InterlockedDecrement(&Object->Refs);

    assert(Refs >= 0);
    if (!Refs)
        WmiObjectFree(Object);
    return Refs;
}

static HRESULT
WmiQualifierSetCreate(WMI_OBJECT *Owner, const WCHAR *Property, const WCHAR *Method, IWbemQualifierSet **Result)
{
    WMI_QUALIFIER_SET *Set = calloc(1, sizeof(*Set));

    if (!Set)
        return WBEM_E_OUT_OF_MEMORY;
    Set->IWbemQualifierSet_iface.lpVtbl = &WmiQualifierSetVtbl;
    Set->Refs = 1;
    Set->Owner = Owner;
    Set->Property = WmiStrDup(Property);
    Set->Method = WmiStrDup(Method);
    if ((Property && !Set->Property) || (Method && !Set->Method))
    {
        free(Set->Property);
        free(Set->Method);
        free(Set);
        return WBEM_E_OUT_OF_MEMORY;
    }
    IWbemClassObject_AddRef(&Owner->IWbemClassObject_iface);
    *Result = &Set->IWbemQualifierSet_iface;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_GetQualifierSet(IWbemClassObject *Iface, IWbemQualifierSet **Result)
{
    if (!Result)
        return WBEM_E_INVALID_PARAMETER;
    return WmiQualifierSetCreate(WmiObjectFromIface(Iface), NULL, NULL, Result);
}

static HRESULT WINAPI
WmiObject_Get(IWbemClassObject *Iface, LPCWSTR Name, LONG Flags, VARIANT *Value, CIMTYPE *Type, LONG *Flavor)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WMI_PROPERTY *Property;
    VARIANT Local;
    CIMTYPE LocalType;
    HRESULT hr;

    if (!Name)
        return WBEM_E_INVALID_PARAMETER;
    if (Name[0] == L'_' && Name[1] == L'_')
    {
        hr = WmiSystemProperty(Object, Name, &Local, &LocalType);
        if (FAILED(hr))
            return hr;
        if (Value)
            *Value = Local;
        else
            VariantClear(&Local);
        if (Type)
            *Type = LocalType;
        if (Flavor)
            *Flavor = WBEM_FLAVOR_ORIGIN_SYSTEM;
        return S_OK;
    }
    if (!(Property = WmiPropertyFind(Object, Name)))
        return WBEM_E_NOT_FOUND;
    if (Value)
    {
        hr = WmiCopyVariant(Value, &Property->Value);
        if (FAILED(hr))
            return hr;
    }
    if (Type)
        *Type = Property->Type;
    if (Flavor)
        *Flavor = WmiPropertyFlavor(Object, Property);
    return S_OK;
}

static HRESULT
WmiDerivationSet(WMI_OBJECT *Object, WMI_OBJECT *Base)
{
    ULONG Index;

    if (!(Object->Derivation = calloc(Base->DerivationCount + 1, sizeof(WCHAR *))))
        return WBEM_E_OUT_OF_MEMORY;
    if (!(Object->Derivation[0] = _wcsdup(Base->Class)))
        return WBEM_E_OUT_OF_MEMORY;
    Object->DerivationCount = 1;
    for (Index = 0; Index < Base->DerivationCount; Index++)
    {
        if (!(Object->Derivation[Index + 1] = _wcsdup(Base->Derivation[Index])))
            return WBEM_E_OUT_OF_MEMORY;
        Object->DerivationCount++;
    }
    return S_OK;
}

static HRESULT WINAPI
WmiObject_Put(IWbemClassObject *Iface, LPCWSTR Name, LONG Flags, VARIANT *Value, CIMTYPE Type)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WMI_PROPERTY *Property;
    VARIANT Converted;
    HRESULT hr;

    if (!Name)
        return WBEM_E_INVALID_PARAMETER;
    if (Name[0] == L'_' && Name[1] == L'_')
    {
        if (_wcsicmp(Name, L"__CLASS") || Object->Genus != WBEM_GENUS_CLASS || !Value || V_VT(Value) != VT_BSTR)
            return WBEM_E_READ_ONLY;
        free(Object->Class);
        if (!(Object->Class = _wcsdup(V_BSTR(Value))))
            return WBEM_E_OUT_OF_MEMORY;
        return S_OK;
    }
    Property = WmiPropertyFind(Object, Name);
    if (!Property)
    {
        WMI_PROPERTY *Grown;

        if (Object->Genus != WBEM_GENUS_CLASS)
            return WBEM_E_NOT_FOUND;
        if (!Type)
        {
            if (!Value || V_VT(Value) == VT_NULL || V_VT(Value) == VT_EMPTY)
                return WBEM_E_INVALID_PARAMETER;
            Type = WmiCimTypeOf(Value);
        }
        if (!(Grown = realloc(Object->Properties, (Object->PropertyCount + 1) * sizeof(*Grown))))
            return WBEM_E_OUT_OF_MEMORY;
        Object->Properties = Grown;
        Property = &Grown[Object->PropertyCount];
        memset(Property, 0, sizeof(*Property));
        VariantInit(&Property->Value);
        V_VT(&Property->Value) = VT_NULL;
        if (!(Property->Name = _wcsdup(Name)))
            return WBEM_E_OUT_OF_MEMORY;
        Property->Type = Type;
        Object->PropertyCount++;
    }
    else if (Type && Type != Property->Type)
    {
        if (Object->Genus != WBEM_GENUS_CLASS || WmiPropertyFlavor(Object, Property) != WBEM_FLAVOR_ORIGIN_LOCAL)
            return WBEM_E_TYPE_MISMATCH;
        Property->Type = Type;
    }
    hr = WmiCoerce(Value, Property->Type, &Converted);
    if (FAILED(hr))
        return hr;
    VariantClear(&Property->Value);
    Property->Value = Converted;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_Delete(IWbemClassObject *Iface, LPCWSTR Name)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WMI_PROPERTY *Property;
    ULONG Index;

    if (!Name)
        return WBEM_E_INVALID_PARAMETER;
    if (Name[0] == L'_' && Name[1] == L'_')
        return WBEM_E_SYSTEM_PROPERTY;
    if (!(Property = WmiPropertyFind(Object, Name)))
        return WBEM_E_NOT_FOUND;
    if (Object->Genus != WBEM_GENUS_CLASS)
    {
        /* TODO: an instance property returns to the class default value */
        VariantClear(&Property->Value);
        V_VT(&Property->Value) = VT_NULL;
        return WBEM_S_RESET_TO_DEFAULT;
    }
    if (WmiPropertyFlavor(Object, Property) != WBEM_FLAVOR_ORIGIN_LOCAL)
        return WBEM_E_PROPAGATED_PROPERTY;
    Index = (ULONG)(Property - Object->Properties);
    free(Property->Name);
    free(Property->Origin);
    VariantClear(&Property->Value);
    WmiQualifiersFree(&Property->Qualifiers);
    memmove(Property, Property + 1, (Object->PropertyCount - Index - 1) * sizeof(*Property));
    Object->PropertyCount--;
    return S_OK;
}

static BOOL
WmiNameSelected(WMI_OBJECT *Object, WMI_PROPERTY *Property, LONG Flags, const WCHAR *Qualifier,
                const VARIANT *QualifierValue)
{
    LONG Origin = Flags & WBEM_MASK_CONDITION_ORIGIN;
    LONG Condition = Flags & WBEM_MASK_PRIMARY_CONDITION;

    if (Origin == WBEM_FLAG_SYSTEM_ONLY)
        return FALSE;
    if (Origin == WBEM_FLAG_LOCAL_ONLY && WmiPropertyFlavor(Object, Property) != WBEM_FLAVOR_ORIGIN_LOCAL)
        return FALSE;
    if (Origin == WBEM_FLAG_PROPAGATED_ONLY && WmiPropertyFlavor(Object, Property) != WBEM_FLAVOR_ORIGIN_PROPAGATED)
        return FALSE;
    if ((Flags & WBEM_FLAG_KEYS_ONLY) && !WmiPropertyIsKey(Property))
        return FALSE;
    if ((Flags & WBEM_FLAG_REFS_ONLY) && Property->Type != CIM_REFERENCE)
        return FALSE;
    if (Qualifier)
    {
        WMI_QUALIFIER *Found = WmiQualifierFind(&Property->Qualifiers, Qualifier);

        if (Condition == WBEM_FLAG_ONLY_IF_TRUE && !Found)
            return FALSE;
        if (Condition == WBEM_FLAG_ONLY_IF_FALSE && Found)
            return FALSE;
        if (Condition == WBEM_FLAG_ONLY_IF_IDENTICAL &&
            (!Found || !QualifierValue || VarCmp(&Found->Value, (VARIANT *)QualifierValue, 0, 0) != VARCMP_EQ))
            return FALSE;
    }
    return TRUE;
}

static HRESULT WINAPI
WmiObject_GetNames(IWbemClassObject *Iface, LPCWSTR Qualifier, LONG Flags, VARIANT *QualifierValue,
                   SAFEARRAY **Names)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    LONG Origin = Flags & WBEM_MASK_CONDITION_ORIGIN;
    BOOL System = !Qualifier && !(Flags & (WBEM_FLAG_KEYS_ONLY | WBEM_FLAG_REFS_ONLY)) &&
                  (Origin == WBEM_FLAG_ALWAYS || Origin == WBEM_FLAG_SYSTEM_ONLY);
    SAFEARRAY *Array;
    LONG Count = 0, Index;
    ULONG Item;

    if (!Names)
        return WBEM_E_INVALID_PARAMETER;
    if (!(Array = SafeArrayCreateVector(VT_BSTR, 0, ARRAYSIZE(SystemProperties) + Object->PropertyCount)))
        return WBEM_E_OUT_OF_MEMORY;
    if (System)
    {
        for (Item = 0; Item < ARRAYSIZE(SystemProperties); Item++)
        {
            BSTR Name = SysAllocString(SystemProperties[Item]);

            SafeArrayPutElement(Array, &Count, Name);
            SysFreeString(Name);
            Count++;
        }
    }
    if (Origin != WBEM_FLAG_SYSTEM_ONLY)
    {
        for (Item = 0; Item < Object->PropertyCount; Item++)
        {
            BSTR Name;

            if (!WmiNameSelected(Object, &Object->Properties[Item], Flags, Qualifier, QualifierValue))
                continue;
            Name = SysAllocString(Object->Properties[Item].Name);
            SafeArrayPutElement(Array, &Count, Name);
            SysFreeString(Name);
            Count++;
        }
    }
    {
        SAFEARRAYBOUND Bound = { (ULONG)Count, 0 };

        SafeArrayRedim(Array, &Bound);
    }
    (void)Index;
    *Names = Array;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_BeginEnumeration(IWbemClassObject *Iface, LONG Flags)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);

    Object->Enumerating = TRUE;
    Object->EnumFlags = Flags;
    Object->EnumIndex = 0;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_Next(IWbemClassObject *Iface, LONG Flags, BSTR *Name, VARIANT *Value, CIMTYPE *Type, LONG *Flavor)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    LONG Origin = Object->EnumFlags & WBEM_MASK_CONDITION_ORIGIN;
    BOOL System = !(Object->EnumFlags & (WBEM_FLAG_KEYS_ONLY | WBEM_FLAG_REFS_ONLY)) &&
                  (Origin == WBEM_FLAG_ALWAYS || Origin == WBEM_FLAG_SYSTEM_ONLY);

    if (!Object->Enumerating)
        return WBEM_E_UNEXPECTED;
    for (;;)
    {
        ULONG Index = Object->EnumIndex;
        const WCHAR *Current;

        if (Index < ARRAYSIZE(SystemProperties))
        {
            Object->EnumIndex++;
            if (!System)
                continue;
            Current = SystemProperties[Index];
        }
        else if (Index - ARRAYSIZE(SystemProperties) < Object->PropertyCount)
        {
            WMI_PROPERTY *Property = &Object->Properties[Index - ARRAYSIZE(SystemProperties)];

            Object->EnumIndex++;
            if (Origin == WBEM_FLAG_SYSTEM_ONLY || !WmiNameSelected(Object, Property, Object->EnumFlags, NULL, NULL))
                continue;
            Current = Property->Name;
        }
        else
            return WBEM_S_NO_MORE_DATA;
        if (Name && !(*Name = SysAllocString(Current)))
            return WBEM_E_OUT_OF_MEMORY;
        return WmiObject_Get(Iface, Current, 0, Value, Type, Flavor);
    }
}

static HRESULT WINAPI
WmiObject_EndEnumeration(IWbemClassObject *Iface)
{
    WmiObjectFromIface(Iface)->Enumerating = FALSE;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_GetPropertyQualifierSet(IWbemClassObject *Iface, LPCWSTR Property, IWbemQualifierSet **Result)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);

    if (!Property || !Result)
        return WBEM_E_INVALID_PARAMETER;
    if (Property[0] == L'_' && Property[1] == L'_')
        return WBEM_E_SYSTEM_PROPERTY;
    if (!WmiPropertyFind(Object, Property))
        return WBEM_E_NOT_FOUND;
    return WmiQualifierSetCreate(Object, Property, NULL, Result);
}

static HRESULT WINAPI
WmiObject_Clone(IWbemClassObject *Iface, IWbemClassObject **Result)
{
    WMI_OBJECT *Copy;
    HRESULT hr;

    if (!Result)
        return WBEM_E_INVALID_PARAMETER;
    hr = WmiCloneObject(WmiObjectFromIface(Iface), &Copy);
    if (SUCCEEDED(hr))
        *Result = &Copy->IWbemClassObject_iface;
    return hr;
}

static void
WmiTextAppend(WCHAR **Text, SIZE_T *Length, const WCHAR *Part)
{
    SIZE_T Add = wcslen(Part);
    WCHAR *Grown;

    if (!*Text)
        return;
    if (!(Grown = realloc(*Text, (*Length + Add + 1) * sizeof(WCHAR))))
    {
        free(*Text);
        *Text = NULL;
        return;
    }
    memcpy(Grown + *Length, Part, (Add + 1) * sizeof(WCHAR));
    *Text = Grown;
    *Length += Add;
}

static HRESULT WINAPI
WmiObject_GetObjectText(IWbemClassObject *Iface, LONG Flags, BSTR *Result)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WCHAR *Text = _wcsdup(L""), Line[512];
    SIZE_T Length = 0;
    ULONG Index;

    if (!Result)
        return WBEM_E_INVALID_PARAMETER;
    if (Object->Genus == WBEM_GENUS_CLASS)
    {
        swprintf(Line, ARRAYSIZE(Line), L"class %ls%ls%ls\n{\n", Object->Class ? Object->Class : L"",
                 Object->Super ? L" : " : L"", Object->Super ? Object->Super : L"");
        WmiTextAppend(&Text, &Length, Line);
        for (Index = 0; Index < Object->PropertyCount; Index++)
        {
            WMI_PROPERTY *Property = &Object->Properties[Index];

            if (WmiPropertyFlavor(Object, Property) != WBEM_FLAVOR_ORIGIN_LOCAL)
                continue;
            swprintf(Line, ARRAYSIZE(Line), L"\t%ls %ls%ls;\n", WmiTypeName(Property->Type), Property->Name,
                     (Property->Type & CIM_FLAG_ARRAY) ? L"[]" : L"");
            WmiTextAppend(&Text, &Length, Line);
        }
    }
    else
    {
        swprintf(Line, ARRAYSIZE(Line), L"instance of %ls\n{\n", Object->Class ? Object->Class : L"");
        WmiTextAppend(&Text, &Length, Line);
        for (Index = 0; Index < Object->PropertyCount; Index++)
        {
            WMI_PROPERTY *Property = &Object->Properties[Index];
            VARIANT Converted;

            if (V_VT(&Property->Value) == VT_NULL || (V_VT(&Property->Value) & VT_ARRAY) ||
                V_VT(&Property->Value) == VT_UNKNOWN)
                continue;
            VariantInit(&Converted);
            if (FAILED(VariantChangeType(&Converted, &Property->Value, 0, VT_BSTR)))
                continue;
            swprintf(Line, ARRAYSIZE(Line), Property->Type == CIM_STRING || Property->Type == CIM_DATETIME ?
                     L"\t%ls = \"%ls\";\n" : L"\t%ls = %ls;\n", Property->Name, V_BSTR(&Converted));
            VariantClear(&Converted);
            WmiTextAppend(&Text, &Length, Line);
        }
    }
    WmiTextAppend(&Text, &Length, L"};\n");
    if (!Text)
        return WBEM_E_OUT_OF_MEMORY;
    *Result = SysAllocString(Text);
    free(Text);
    return *Result ? S_OK : WBEM_E_OUT_OF_MEMORY;
}

static HRESULT
WmiPropertiesPropagate(WMI_OBJECT *Target, WMI_OBJECT *Source, LONG Required)
{
    ULONG Index;
    HRESULT hr;

    if (!Source->PropertyCount)
        return S_OK;
    if (!(Target->Properties = calloc(Source->PropertyCount, sizeof(*Target->Properties))))
        return WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; Index < Source->PropertyCount; Index++)
    {
        WMI_PROPERTY *From = &Source->Properties[Index], *To = &Target->Properties[Index];

        Target->PropertyCount = Index + 1;
        VariantInit(&To->Value);
        To->Name = _wcsdup(From->Name);
        To->Type = From->Type;
        To->Origin = WmiStrDup(WmiOriginOf(Source, From));
        if (!To->Name || !To->Origin)
            return WBEM_E_OUT_OF_MEMORY;
        hr = WmiCopyVariant(&To->Value, &From->Value);
        if (SUCCEEDED(hr))
            hr = WmiQualifiersCopy(&To->Qualifiers, &From->Qualifiers, Required);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static HRESULT WINAPI
WmiObject_SpawnDerivedClass(IWbemClassObject *Iface, LONG Flags, IWbemClassObject **Result)
{
    WMI_OBJECT *Base = WmiObjectFromIface(Iface), *Derived;
    ULONG Index;
    HRESULT hr;

    if (!Result || Flags)
        return WBEM_E_INVALID_PARAMETER;
    if (Base->Genus != WBEM_GENUS_CLASS || !Base->Class)
        return WBEM_E_INVALID_OPERATION;
    if (!(Derived = WmiObjectAlloc(WBEM_GENUS_CLASS)))
        return WBEM_E_OUT_OF_MEMORY;
    hr = (Derived->Super = _wcsdup(Base->Class)) ? S_OK : WBEM_E_OUT_OF_MEMORY;
    if (SUCCEEDED(hr))
        hr = WmiDerivationSet(Derived, Base);
    if (SUCCEEDED(hr))
        hr = WmiQualifiersCopy(&Derived->Qualifiers, &Base->Qualifiers, WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS);
    if (SUCCEEDED(hr))
        hr = WmiPropertiesPropagate(Derived, Base, WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS);
    if (SUCCEEDED(hr) && Base->MethodCount && !(Derived->Methods = calloc(Base->MethodCount, sizeof(WMI_METHOD))))
        hr = WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; SUCCEEDED(hr) && Index < Base->MethodCount; Index++)
    {
        WMI_METHOD *From = &Base->Methods[Index], *To = &Derived->Methods[Index];

        Derived->MethodCount = Index + 1;
        To->Name = _wcsdup(From->Name);
        To->Origin = _wcsdup(From->Origin ? From->Origin : Base->Class);
        if (!To->Name || !To->Origin)
            hr = WBEM_E_OUT_OF_MEMORY;
        if (SUCCEEDED(hr) && From->In)
            hr = IWbemClassObject_Clone(From->In, &To->In);
        if (SUCCEEDED(hr) && From->Out)
            hr = IWbemClassObject_Clone(From->Out, &To->Out);
        if (SUCCEEDED(hr))
            hr = WmiQualifiersCopy(&To->Qualifiers, &From->Qualifiers, WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS);
    }
    if (SUCCEEDED(hr))
    {
        Derived->Server = WmiStrDup(Base->Server);
        Derived->Namespace = WmiStrDup(Base->Namespace);
    }
    if (FAILED(hr))
    {
        WmiObjectFree(Derived);
        return hr;
    }
    *Result = &Derived->IWbemClassObject_iface;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_SpawnInstance(IWbemClassObject *Iface, LONG Flags, IWbemClassObject **Result)
{
    WMI_OBJECT *Class = WmiObjectFromIface(Iface), *Instance;
    ULONG Index;
    HRESULT hr;

    if (!Result || Flags)
        return WBEM_E_INVALID_PARAMETER;
    if (Class->Genus != WBEM_GENUS_CLASS || !Class->Class)
        return WBEM_E_INVALID_OPERATION;
    if (!(Instance = WmiObjectAlloc(WBEM_GENUS_INSTANCE)))
        return WBEM_E_OUT_OF_MEMORY;
    hr = (Instance->Class = _wcsdup(Class->Class)) ? S_OK : WBEM_E_OUT_OF_MEMORY;
    if (SUCCEEDED(hr) && Class->Super && !(Instance->Super = _wcsdup(Class->Super)))
        hr = WBEM_E_OUT_OF_MEMORY;
    if (SUCCEEDED(hr) && Class->DerivationCount && !(Instance->Derivation = calloc(Class->DerivationCount, sizeof(WCHAR *))))
        hr = WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; SUCCEEDED(hr) && Index < Class->DerivationCount; Index++)
    {
        Instance->DerivationCount = Index + 1;
        if (!(Instance->Derivation[Index] = _wcsdup(Class->Derivation[Index])))
            hr = WBEM_E_OUT_OF_MEMORY;
    }
    if (SUCCEEDED(hr))
        hr = WmiQualifiersCopy(&Instance->Qualifiers, &Class->Qualifiers, WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE);
    if (SUCCEEDED(hr))
        hr = WmiPropertiesPropagate(Instance, Class, WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE);
    if (SUCCEEDED(hr))
    {
        Instance->Server = WmiStrDup(Class->Server);
        Instance->Namespace = WmiStrDup(Class->Namespace);
        /* Instances keep the property origins of their class; a property defined by the class itself is local */
        for (Index = 0; Index < Instance->PropertyCount; Index++)
        {
            WMI_PROPERTY *Property = &Instance->Properties[Index];

            if (Property->Origin && !_wcsicmp(Property->Origin, Class->Class))
            {
                free(Property->Origin);
                Property->Origin = NULL;
            }
        }
    }
    if (FAILED(hr))
    {
        WmiObjectFree(Instance);
        return hr;
    }
    *Result = &Instance->IWbemClassObject_iface;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_CompareTo(IWbemClassObject *Iface, LONG Flags, IWbemClassObject *Other)
{
    WMI_BUFFER Left = { 0 }, Right = { 0 };
    HRESULT hr;

    if (!Other)
        return WBEM_E_INVALID_PARAMETER;
    /* TODO: honour the WBEM_FLAG_IGNORE_* comparison flags */
    hr = WmiObjectSerialize(Iface, &Left);
    if (SUCCEEDED(hr))
        hr = WmiObjectSerialize(Other, &Right);
    if (SUCCEEDED(hr))
        hr = (Left.Size == Right.Size && !memcmp(Left.Data, Right.Data, Left.Size)) ? WBEM_S_SAME : WBEM_S_DIFFERENT;
    WmiBufferFree(&Left);
    WmiBufferFree(&Right);
    return hr;
}

static HRESULT WINAPI
WmiObject_GetPropertyOrigin(IWbemClassObject *Iface, LPCWSTR Name, BSTR *Result)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WMI_PROPERTY *Property;

    if (!Name || !Result)
        return WBEM_E_INVALID_PARAMETER;
    if (!(Property = WmiPropertyFind(Object, Name)))
        return WBEM_E_NOT_FOUND;
    *Result = SysAllocString(WmiOriginOf(Object, Property));
    return *Result ? S_OK : WBEM_E_OUT_OF_MEMORY;
}

static HRESULT WINAPI
WmiObject_InheritsFrom(IWbemClassObject *Iface, LPCWSTR Ancestor)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    ULONG Index;

    if (!Ancestor)
        return WBEM_E_INVALID_PARAMETER;
    for (Index = 0; Index < Object->DerivationCount; Index++)
        if (!_wcsicmp(Object->Derivation[Index], Ancestor))
            return WBEM_S_NO_ERROR;
    return WBEM_S_FALSE;
}

static HRESULT WINAPI
WmiObject_GetMethod(IWbemClassObject *Iface, LPCWSTR Name, LONG Flags, IWbemClassObject **In,
                    IWbemClassObject **Out)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WMI_METHOD *Method;
    HRESULT hr = S_OK;

    if (!Name)
        return WBEM_E_INVALID_PARAMETER;
    if (In)
        *In = NULL;
    if (Out)
        *Out = NULL;
    if (!(Method = WmiMethodFind(Object, Name)))
        return WBEM_E_NOT_FOUND;
    if (In && Method->In)
        hr = IWbemClassObject_Clone(Method->In, In);
    if (SUCCEEDED(hr) && Out && Method->Out)
        hr = IWbemClassObject_Clone(Method->Out, Out);
    if (FAILED(hr) && In && *In)
    {
        IWbemClassObject_Release(*In);
        *In = NULL;
    }
    return hr;
}

static HRESULT WINAPI
WmiObject_PutMethod(IWbemClassObject *Iface, LPCWSTR Name, LONG Flags, IWbemClassObject *In,
                    IWbemClassObject *Out)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WMI_METHOD *Method;
    IWbemClassObject *InCopy = NULL, *OutCopy = NULL;
    HRESULT hr = S_OK;

    if (!Name)
        return WBEM_E_INVALID_PARAMETER;
    if (Object->Genus != WBEM_GENUS_CLASS)
        return WBEM_E_INVALID_OPERATION;
    if (In)
        hr = IWbemClassObject_Clone(In, &InCopy);
    if (SUCCEEDED(hr) && Out)
        hr = IWbemClassObject_Clone(Out, &OutCopy);
    if (FAILED(hr))
    {
        if (InCopy)
            IWbemClassObject_Release(InCopy);
        return hr;
    }
    if (!(Method = WmiMethodFind(Object, Name)))
    {
        WMI_METHOD *Grown = realloc(Object->Methods, (Object->MethodCount + 1) * sizeof(*Grown));

        if (!Grown)
            hr = WBEM_E_OUT_OF_MEMORY;
        else
        {
            Object->Methods = Grown;
            Method = &Grown[Object->MethodCount];
            memset(Method, 0, sizeof(*Method));
            if (!(Method->Name = _wcsdup(Name)))
                hr = WBEM_E_OUT_OF_MEMORY;
            else
                Object->MethodCount++;
        }
    }
    if (FAILED(hr))
    {
        if (InCopy)
            IWbemClassObject_Release(InCopy);
        if (OutCopy)
            IWbemClassObject_Release(OutCopy);
        return hr;
    }
    if (Method->In)
        IWbemClassObject_Release(Method->In);
    if (Method->Out)
        IWbemClassObject_Release(Method->Out);
    Method->In = InCopy;
    Method->Out = OutCopy;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_DeleteMethod(IWbemClassObject *Iface, LPCWSTR Name)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WMI_METHOD *Method;
    ULONG Index;

    if (!Name)
        return WBEM_E_INVALID_PARAMETER;
    if (!(Method = WmiMethodFind(Object, Name)))
        return WBEM_E_NOT_FOUND;
    if (Method->Origin && Object->Class && _wcsicmp(Method->Origin, Object->Class))
        return WBEM_E_PROPAGATED_METHOD;
    Index = (ULONG)(Method - Object->Methods);
    free(Method->Name);
    free(Method->Origin);
    if (Method->In)
        IWbemClassObject_Release(Method->In);
    if (Method->Out)
        IWbemClassObject_Release(Method->Out);
    WmiQualifiersFree(&Method->Qualifiers);
    memmove(Method, Method + 1, (Object->MethodCount - Index - 1) * sizeof(*Method));
    Object->MethodCount--;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_BeginMethodEnumeration(IWbemClassObject *Iface, LONG Flags)
{
    WmiObjectFromIface(Iface)->MethodIndex = 0;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_NextMethod(IWbemClassObject *Iface, LONG Flags, BSTR *Name, IWbemClassObject **In,
                     IWbemClassObject **Out)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WMI_METHOD *Method;

    if (Object->MethodIndex >= Object->MethodCount)
        return WBEM_S_NO_MORE_DATA;
    Method = &Object->Methods[Object->MethodIndex++];
    if (Name && !(*Name = SysAllocString(Method->Name)))
        return WBEM_E_OUT_OF_MEMORY;
    return WmiObject_GetMethod(Iface, Method->Name, 0, In, Out);
}

static HRESULT WINAPI
WmiObject_EndMethodEnumeration(IWbemClassObject *Iface)
{
    WmiObjectFromIface(Iface)->MethodIndex = 0;
    return S_OK;
}

static HRESULT WINAPI
WmiObject_GetMethodQualifierSet(IWbemClassObject *Iface, LPCWSTR Method, IWbemQualifierSet **Result)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);

    if (!Method || !Result)
        return WBEM_E_INVALID_PARAMETER;
    if (!WmiMethodFind(Object, Method))
        return WBEM_E_NOT_FOUND;
    return WmiQualifierSetCreate(Object, NULL, Method, Result);
}

static HRESULT WINAPI
WmiObject_GetMethodOrigin(IWbemClassObject *Iface, LPCWSTR Name, BSTR *Result)
{
    WMI_OBJECT *Object = WmiObjectFromIface(Iface);
    WMI_METHOD *Method;

    if (!Name || !Result)
        return WBEM_E_INVALID_PARAMETER;
    if (!(Method = WmiMethodFind(Object, Name)))
        return WBEM_E_NOT_FOUND;
    *Result = SysAllocString(Method->Origin ? Method->Origin : Object->Class);
    return *Result ? S_OK : WBEM_E_OUT_OF_MEMORY;
}

static const IWbemClassObjectVtbl WmiObjectVtbl =
{
    WmiObject_QueryInterface,
    WmiObject_AddRef,
    WmiObject_Release,
    WmiObject_GetQualifierSet,
    WmiObject_Get,
    WmiObject_Put,
    WmiObject_Delete,
    WmiObject_GetNames,
    WmiObject_BeginEnumeration,
    WmiObject_Next,
    WmiObject_EndEnumeration,
    WmiObject_GetPropertyQualifierSet,
    WmiObject_Clone,
    WmiObject_GetObjectText,
    WmiObject_SpawnDerivedClass,
    WmiObject_SpawnInstance,
    WmiObject_CompareTo,
    WmiObject_GetPropertyOrigin,
    WmiObject_InheritsFrom,
    WmiObject_GetMethod,
    WmiObject_PutMethod,
    WmiObject_DeleteMethod,
    WmiObject_BeginMethodEnumeration,
    WmiObject_NextMethod,
    WmiObject_EndMethodEnumeration,
    WmiObject_GetMethodQualifierSet,
    WmiObject_GetMethodOrigin
};

/* IWbemQualifierSet: a live view of the qualifiers of the object, a property or a method */

static inline WMI_QUALIFIER_SET *
WmiQualifierSetFromIface(IWbemQualifierSet *Iface)
{
    return CONTAINING_RECORD(Iface, WMI_QUALIFIER_SET, IWbemQualifierSet_iface);
}

static WMI_QUALIFIERS *
WmiQualifierSetList(WMI_QUALIFIER_SET *Set, WMI_PROPERTY **Property)
{
    *Property = NULL;
    if (Set->Property)
    {
        *Property = WmiPropertyFind(Set->Owner, Set->Property);
        return *Property ? &(*Property)->Qualifiers : NULL;
    }
    if (Set->Method)
    {
        WMI_METHOD *Method = WmiMethodFind(Set->Owner, Set->Method);

        return Method ? &Method->Qualifiers : NULL;
    }
    return &Set->Owner->Qualifiers;
}

static HRESULT WINAPI
WmiQualifierSet_QueryInterface(IWbemQualifierSet *Iface, REFIID Iid, void **Result)
{
    if (!Result)
        return E_POINTER;
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IWbemQualifierSet))
    {
        *Result = Iface;
        IWbemQualifierSet_AddRef(Iface);
        return S_OK;
    }
    *Result = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
WmiQualifierSet_AddRef(IWbemQualifierSet *Iface)
{
    return InterlockedIncrement(&WmiQualifierSetFromIface(Iface)->Refs);
}

static ULONG WINAPI
WmiQualifierSet_Release(IWbemQualifierSet *Iface)
{
    WMI_QUALIFIER_SET *Set = WmiQualifierSetFromIface(Iface);
    LONG Refs = InterlockedDecrement(&Set->Refs);

    if (!Refs)
    {
        IWbemClassObject_Release(&Set->Owner->IWbemClassObject_iface);
        free(Set->Property);
        free(Set->Method);
        free(Set);
    }
    return Refs;
}

static HRESULT WINAPI
WmiQualifierSet_Get(IWbemQualifierSet *Iface, LPCWSTR Name, LONG Flags, VARIANT *Value, LONG *Flavor)
{
    WMI_QUALIFIER_SET *Set = WmiQualifierSetFromIface(Iface);
    WMI_PROPERTY *Property;
    WMI_QUALIFIERS *List = WmiQualifierSetList(Set, &Property);
    WMI_QUALIFIER *Qualifier;

    if (!Name)
        return WBEM_E_INVALID_PARAMETER;
    if (!List)
        return WBEM_E_NOT_FOUND;
    if (!(Qualifier = WmiQualifierFind(List, Name)))
    {
        if (Property && !_wcsicmp(Name, L"CIMTYPE"))
        {
            if (Value)
            {
                V_VT(Value) = VT_BSTR;
                V_BSTR(Value) = SysAllocString(WmiTypeName(Property->Type));
            }
            if (Flavor)
                *Flavor = WBEM_FLAVOR_ORIGIN_PROPAGATED;
            return S_OK;
        }
        return WBEM_E_NOT_FOUND;
    }
    if (Value)
    {
        HRESULT hr = WmiCopyVariant(Value, &Qualifier->Value);

        if (FAILED(hr))
            return hr;
    }
    if (Flavor)
        *Flavor = Qualifier->Flavor;
    return S_OK;
}

static HRESULT WINAPI
WmiQualifierSet_Put(IWbemQualifierSet *Iface, LPCWSTR Name, VARIANT *Value, LONG Flavor)
{
    WMI_QUALIFIER_SET *Set = WmiQualifierSetFromIface(Iface);
    WMI_PROPERTY *Property;
    WMI_QUALIFIERS *List = WmiQualifierSetList(Set, &Property);
    WMI_QUALIFIER *Existing;

    if (!Name || !Value)
        return WBEM_E_INVALID_PARAMETER;
    if (!List)
        return WBEM_E_NOT_FOUND;
    if ((Existing = WmiQualifierFind(List, Name)) && (Existing->Flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED) &&
        (Existing->Flavor & WBEM_FLAVOR_NOT_OVERRIDABLE))
        return WBEM_E_OVERRIDE_NOT_ALLOWED;
    return WmiQualifierSet(List, Name, Value, Flavor & ~WBEM_FLAVOR_MASK_ORIGIN);
}

static HRESULT WINAPI
WmiQualifierSet_Delete(IWbemQualifierSet *Iface, LPCWSTR Name)
{
    WMI_QUALIFIER_SET *Set = WmiQualifierSetFromIface(Iface);
    WMI_PROPERTY *Property;
    WMI_QUALIFIERS *List = WmiQualifierSetList(Set, &Property);
    WMI_QUALIFIER *Qualifier;
    ULONG Index;

    if (!Name)
        return WBEM_E_INVALID_PARAMETER;
    if (!List || !(Qualifier = WmiQualifierFind(List, Name)))
        return WBEM_E_NOT_FOUND;
    if (Qualifier->Flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED)
        return WBEM_E_PROPAGATED_QUALIFIER;
    Index = (ULONG)(Qualifier - List->Items);
    free(Qualifier->Name);
    VariantClear(&Qualifier->Value);
    memmove(Qualifier, Qualifier + 1, (List->Count - Index - 1) * sizeof(*Qualifier));
    List->Count--;
    return S_OK;
}

static HRESULT WINAPI
WmiQualifierSet_GetNames(IWbemQualifierSet *Iface, LONG Flags, SAFEARRAY **Names)
{
    WMI_QUALIFIER_SET *Set = WmiQualifierSetFromIface(Iface);
    WMI_PROPERTY *Property;
    WMI_QUALIFIERS *List = WmiQualifierSetList(Set, &Property);
    SAFEARRAY *Array;
    LONG Count = 0;
    ULONG Index;

    if (!Names)
        return WBEM_E_INVALID_PARAMETER;
    if (!List)
        return WBEM_E_NOT_FOUND;
    if (!(Array = SafeArrayCreateVector(VT_BSTR, 0, List->Count)))
        return WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; Index < List->Count; Index++)
    {
        BSTR Name;

        if ((Flags & WBEM_FLAG_LOCAL_ONLY) && (List->Items[Index].Flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED))
            continue;
        if ((Flags & WBEM_FLAG_PROPAGATED_ONLY) && !(List->Items[Index].Flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED))
            continue;
        Name = SysAllocString(List->Items[Index].Name);
        SafeArrayPutElement(Array, &Count, Name);
        SysFreeString(Name);
        Count++;
    }
    {
        SAFEARRAYBOUND Bound = { (ULONG)Count, 0 };

        SafeArrayRedim(Array, &Bound);
    }
    *Names = Array;
    return S_OK;
}

static HRESULT WINAPI
WmiQualifierSet_BeginEnumeration(IWbemQualifierSet *Iface, LONG Flags)
{
    WMI_QUALIFIER_SET *Set = WmiQualifierSetFromIface(Iface);

    Set->EnumIndex = 0;
    Set->EnumFlags = Flags;
    return S_OK;
}

static HRESULT WINAPI
WmiQualifierSet_Next(IWbemQualifierSet *Iface, LONG Flags, BSTR *Name, VARIANT *Value, LONG *Flavor)
{
    WMI_QUALIFIER_SET *Set = WmiQualifierSetFromIface(Iface);
    WMI_PROPERTY *Property;
    WMI_QUALIFIERS *List = WmiQualifierSetList(Set, &Property);

    if (!List)
        return WBEM_E_NOT_FOUND;
    while (Set->EnumIndex < List->Count)
    {
        WMI_QUALIFIER *Qualifier = &List->Items[Set->EnumIndex++];

        if ((Set->EnumFlags & WBEM_FLAG_LOCAL_ONLY) && (Qualifier->Flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED))
            continue;
        if ((Set->EnumFlags & WBEM_FLAG_PROPAGATED_ONLY) && !(Qualifier->Flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED))
            continue;
        if (Name && !(*Name = SysAllocString(Qualifier->Name)))
            return WBEM_E_OUT_OF_MEMORY;
        if (Value)
            WmiCopyVariant(Value, &Qualifier->Value);
        if (Flavor)
            *Flavor = Qualifier->Flavor;
        return S_OK;
    }
    return WBEM_S_NO_MORE_DATA;
}

static HRESULT WINAPI
WmiQualifierSet_EndEnumeration(IWbemQualifierSet *Iface)
{
    WmiQualifierSetFromIface(Iface)->EnumIndex = 0;
    return S_OK;
}

static const IWbemQualifierSetVtbl WmiQualifierSetVtbl =
{
    WmiQualifierSet_QueryInterface,
    WmiQualifierSet_AddRef,
    WmiQualifierSet_Release,
    WmiQualifierSet_Get,
    WmiQualifierSet_Put,
    WmiQualifierSet_Delete,
    WmiQualifierSet_GetNames,
    WmiQualifierSet_BeginEnumeration,
    WmiQualifierSet_Next,
    WmiQualifierSet_EndEnumeration
};
