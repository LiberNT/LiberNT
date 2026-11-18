/*
 * PROJECT:     LiberNT WMI MOF compiler
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Store phase: writes the parsed classes and instances through IWbemServices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "mofd_private.h"
#include <wchar.h>
#include <stdio.h>

typedef struct _MOF_ALIAS
{
    WCHAR *Name;
    BSTR Path;
    struct _MOF_ALIAS *Next;
} MOF_ALIAS;

typedef struct _MOF_STORE
{
    MOF_UNIT *Unit;
    IWbemLocator *Locator;
    IWbemServices *Services;
    WCHAR *Namespace;
    LONG ClassFlags;
    LONG InstanceFlags;
    MOF_ALIAS *Aliases;
} MOF_STORE;

static HRESULT
MofSetNamespace(MOF_STORE *Store, const WCHAR *Path)
{
    SIZE_T Length = wcslen(Path);
    WCHAR *Namespace;

    if (Length == 0)
        return WBEMMOF_E_INVALID_NAMESPACE_SPECIFICATION;
    if ((Path[0] == L'\\' && Path[1] == L'\\') || (Path[0] == L'/' && Path[1] == L'/') ||
        (!_wcsnicmp(Path, L"root", 4) && (Path[4] == 0 || Path[4] == L'\\' || Path[4] == L'/')))
    {
        Namespace = MofStrDup(Store->Unit, Path, Length);
    }
    else
    {
        SIZE_T Current = wcslen(Store->Namespace);

        Namespace = MofAllocArray(Store->Unit, Current + 1 + Length + 1, sizeof(WCHAR));
        if (Namespace)
        {
            memcpy(Namespace, Store->Namespace, Current * sizeof(WCHAR));
            Namespace[Current] = L'\\';
            memcpy(Namespace + Current + 1, Path, (Length + 1) * sizeof(WCHAR));
        }
    }
    if (!Namespace)
        return WBEM_E_OUT_OF_MEMORY;
    Store->Namespace = Namespace;
    if (Store->Services)
    {
        IWbemServices_Release(Store->Services);
        Store->Services = NULL;
    }
    return S_OK;
}

static HRESULT
MofConnect(MOF_STORE *Store)
{
    BSTR Namespace;
    HRESULT Result;

    if (Store->Services)
        return S_OK;
    if (!Store->Locator)
    {
        Result = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator,
                                  (void **)&Store->Locator);
        if (FAILED(Result))
            return Result;
    }
    Namespace = SysAllocString(Store->Namespace);
    if (!Namespace)
        return WBEM_E_OUT_OF_MEMORY;
    Result = IWbemLocator_ConnectServer(Store->Locator, Namespace, NULL, NULL, NULL, 0, NULL, NULL, &Store->Services);
    SysFreeString(Namespace);
    return Result;
}

static CIMTYPE
MofValueType(const MOF_VALUE *Value)
{
    switch (Value->Kind)
    {
    case MofValueInteger: return CIM_SINT32;
    case MofValueReal: return CIM_REAL64;
    case MofValueBoolean: return CIM_BOOLEAN;
    case MofValueChar: return CIM_CHAR16;
    case MofValueString: return CIM_STRING;
    case MofValueAlias: return CIM_REFERENCE;
    case MofValueArray: return Value->Count ? (MofValueType(&Value->Items[0]) | CIM_FLAG_ARRAY) : (CIM_STRING | CIM_FLAG_ARRAY);
    default: return CIM_EMPTY;
    }
}

static HRESULT
MofResolveAlias(MOF_STORE *Store, const WCHAR *Name, BSTR *Path)
{
    MOF_ALIAS *Alias;

    for (Alias = Store->Aliases; Alias; Alias = Alias->Next)
    {
        if (!_wcsicmp(Alias->Name, Name))
        {
            *Path = SysAllocString(Alias->Path);
            return *Path ? S_OK : WBEM_E_OUT_OF_MEMORY;
        }
    }
    return WBEMMOF_E_UNEXPECTED_ALIAS;
}

static BOOL
MofIntegerFits(const MOF_VALUE *Value, CIMTYPE Type)
{
    ULONGLONG Magnitude = Value->Integer;

    switch (Type)
    {
    case CIM_SINT8: return Value->Negative ? Magnitude <= 0x80 : Magnitude <= 0x7F;
    case CIM_UINT8: return !Value->Negative && Magnitude <= 0xFF;
    case CIM_SINT16: return Value->Negative ? Magnitude <= 0x8000 : Magnitude <= 0x7FFF;
    case CIM_UINT16: case CIM_CHAR16: return !Value->Negative && Magnitude <= 0xFFFF;
    case CIM_SINT32: return Value->Negative ? Magnitude <= 0x80000000ULL : Magnitude <= 0x7FFFFFFFULL;
    case CIM_UINT32: return !Value->Negative && Magnitude <= 0xFFFFFFFFULL;
    case CIM_SINT64: return Value->Negative ? Magnitude <= 0x8000000000000000ULL : Magnitude <= 0x7FFFFFFFFFFFFFFFULL;
    case CIM_UINT64: return !Value->Negative;
    default: return TRUE;
    }
}

static HRESULT
MofScalarToVariant(MOF_STORE *Store, const MOF_VALUE *Value, CIMTYPE Type, VARIANT *Variant)
{
    LONGLONG Signed = (LONGLONG)(Value->Negative ? 0ULL - Value->Integer : Value->Integer);
    double Real = Value->Negative ? -(double)Value->Integer : (double)Value->Integer;

    VariantInit(Variant);
    if (Value->Kind == MofValueNull)
    {
        V_VT(Variant) = VT_NULL;
        return S_OK;
    }
    switch (Type)
    {
    case CIM_SINT8: case CIM_UINT8: case CIM_SINT16: case CIM_UINT16:
    case CIM_SINT32: case CIM_UINT32: case CIM_SINT64: case CIM_UINT64: case CIM_CHAR16:
        if (Value->Kind != MofValueInteger && !(Value->Kind == MofValueChar && Type == CIM_CHAR16))
            return WBEMMOF_E_TYPE_MISMATCH;
        if (!MofIntegerFits(Value, Type))
            return WBEMMOF_E_OUT_OF_RANGE;
        if (Type == CIM_UINT8)
        {
            V_VT(Variant) = VT_UI1;
            V_UI1(Variant) = (BYTE)Value->Integer;
        }
        else if (Type == CIM_SINT8 || Type == CIM_SINT16 || Type == CIM_CHAR16)
        {
            V_VT(Variant) = VT_I2;
            V_I2(Variant) = (SHORT)Signed;
        }
        else if (Type == CIM_SINT64 || Type == CIM_UINT64)
        {
            WCHAR Text[24];

            if (Type == CIM_UINT64)
                swprintf(Text, ARRAYSIZE(Text), L"%I64u", Value->Integer);
            else
                swprintf(Text, ARRAYSIZE(Text), L"%I64d", Signed);
            V_VT(Variant) = VT_BSTR;
            V_BSTR(Variant) = SysAllocString(Text);
            if (!V_BSTR(Variant))
                return WBEM_E_OUT_OF_MEMORY;
        }
        else
        {
            V_VT(Variant) = VT_I4;
            V_I4(Variant) = (LONG)Signed;
        }
        return S_OK;
    case CIM_REAL32:
    case CIM_REAL64:
        if (Value->Kind != MofValueReal && Value->Kind != MofValueInteger)
            return WBEMMOF_E_TYPE_MISMATCH;
        if (Type == CIM_REAL32)
        {
            V_VT(Variant) = VT_R4;
            V_R4(Variant) = (float)(Value->Kind == MofValueReal ? Value->Real : Real);
        }
        else
        {
            V_VT(Variant) = VT_R8;
            V_R8(Variant) = Value->Kind == MofValueReal ? Value->Real : Real;
        }
        return S_OK;
    case CIM_BOOLEAN:
        if (Value->Kind != MofValueBoolean)
            return WBEMMOF_E_TYPE_MISMATCH;
        V_VT(Variant) = VT_BOOL;
        V_BOOL(Variant) = Value->Boolean ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    case CIM_STRING:
    case CIM_DATETIME:
    case CIM_REFERENCE:
        if (Value->Kind == MofValueAlias)
        {
            V_VT(Variant) = VT_BSTR;
            return MofResolveAlias(Store, Value->String, &V_BSTR(Variant));
        }
        if (Value->Kind != MofValueString)
            return WBEMMOF_E_TYPE_MISMATCH;
        V_VT(Variant) = VT_BSTR;
        V_BSTR(Variant) = SysAllocString(Value->String);
        return V_BSTR(Variant) ? S_OK : WBEM_E_OUT_OF_MEMORY;
    default:
        /* TODO: embedded object values (CIM_OBJECT) */
        return WBEMMOF_E_TYPE_MISMATCH;
    }
}

static VARTYPE
MofElementVarType(CIMTYPE Type)
{
    switch (Type)
    {
    case CIM_UINT8: return VT_UI1;
    case CIM_SINT8: case CIM_SINT16: case CIM_CHAR16: return VT_I2;
    case CIM_UINT16: case CIM_SINT32: case CIM_UINT32: return VT_I4;
    case CIM_REAL32: return VT_R4;
    case CIM_REAL64: return VT_R8;
    case CIM_BOOLEAN: return VT_BOOL;
    default: return VT_BSTR;
    }
}

static HRESULT
MofToVariant(MOF_STORE *Store, const MOF_VALUE *Value, CIMTYPE Type, VARIANT *Variant)
{
    CIMTYPE Element = Type & ~CIM_FLAG_ARRAY;
    SAFEARRAY *Array;
    VARTYPE ElementVarType;
    ULONG Index;
    HRESULT Result;

    if (Type == CIM_EMPTY || Type == CIM_ILLEGAL)
    {
        Type = MofValueType(Value);
        Element = Type & ~CIM_FLAG_ARRAY;
    }
    if (!(Type & CIM_FLAG_ARRAY) || Value->Kind == MofValueNull)
    {
        if (Value->Kind == MofValueArray)
            return WBEMMOF_E_UNEXPECTED_ARRAY_INIT;
        return MofScalarToVariant(Store, Value, Element, Variant);
    }
    if (Value->Kind != MofValueArray)
        return WBEMMOF_E_TYPE_MISMATCH;
    ElementVarType = MofElementVarType(Element);
    Array = SafeArrayCreateVector(ElementVarType, 0, Value->Count);
    if (!Array)
        return WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; Index < Value->Count; Index++)
    {
        VARIANT Item;
        LONG Position = (LONG)Index;

        Result = MofScalarToVariant(Store, &Value->Items[Index], Element, &Item);
        if (SUCCEEDED(Result))
        {
            if (V_VT(&Item) == VT_BSTR)
                Result = SafeArrayPutElement(Array, &Position, V_BSTR(&Item));
            else
                Result = SafeArrayPutElement(Array, &Position, &V_UI1(&Item));
            VariantClear(&Item);
        }
        if (FAILED(Result))
        {
            SafeArrayDestroy(Array);
            return Result;
        }
    }
    VariantInit(Variant);
    V_VT(Variant) = VT_ARRAY | ElementVarType;
    V_ARRAY(Variant) = Array;
    return S_OK;
}

static HRESULT
MofPutQualifiers(MOF_STORE *Store, IWbemQualifierSet *Set, const MOF_QUALIFIER_LIST *List)
{
    ULONG Index;

    for (Index = 0; Index < List->Count; Index++)
    {
        const MOF_QUALIFIER *Qualifier = &List->Items[Index];
        VARIANT Value;
        HRESULT Result;

        Result = MofToVariant(Store, &Qualifier->Value, CIM_EMPTY, &Value);
        if (FAILED(Result))
            return Result;
        Result = IWbemQualifierSet_Put(Set, Qualifier->Name, &Value, Qualifier->Flavor);
        VariantClear(&Value);
        if (FAILED(Result))
            return Result;
    }
    return S_OK;
}

static HRESULT
MofPutObjectQualifiers(MOF_STORE *Store, IWbemClassObject *Object, const MOF_QUALIFIER_LIST *List)
{
    IWbemQualifierSet *Set;
    HRESULT Result;

    if (!List->Count)
        return S_OK;
    Result = IWbemClassObject_GetQualifierSet(Object, &Set);
    if (FAILED(Result))
        return Result;
    Result = MofPutQualifiers(Store, Set, List);
    IWbemQualifierSet_Release(Set);
    return Result;
}

static HRESULT
MofPutPropertyQualifiers(MOF_STORE *Store, IWbemClassObject *Object, const MOF_PROPERTY *Property)
{
    IWbemQualifierSet *Set;
    HRESULT Result;

    if (!Property->Qualifiers.Count && !Property->ReferenceClass && !Property->ObjectClass)
        return S_OK;
    Result = IWbemClassObject_GetPropertyQualifierSet(Object, Property->Name, &Set);
    if (FAILED(Result))
        return Result;
    Result = MofPutQualifiers(Store, Set, &Property->Qualifiers);
    if (SUCCEEDED(Result) && (Property->ReferenceClass || Property->ObjectClass))
    {
        const WCHAR *Prefix = Property->ReferenceClass ? L"ref:" : L"object:";
        const WCHAR *Class = Property->ReferenceClass ? Property->ReferenceClass : Property->ObjectClass;
        SIZE_T PrefixLength = wcslen(Prefix), Length = wcslen(Class);
        VARIANT Value;

        V_VT(&Value) = VT_BSTR;
        V_BSTR(&Value) = SysAllocStringLen(NULL, (UINT)(PrefixLength + Length));
        if (!V_BSTR(&Value))
        {
            IWbemQualifierSet_Release(Set);
            return WBEM_E_OUT_OF_MEMORY;
        }
        memcpy(V_BSTR(&Value), Prefix, PrefixLength * sizeof(WCHAR));
        memcpy(V_BSTR(&Value) + PrefixLength, Class, Length * sizeof(WCHAR));
        Result = IWbemQualifierSet_Put(Set, L"CIMTYPE", &Value, 0);
        VariantClear(&Value);
    }
    IWbemQualifierSet_Release(Set);
    return Result;
}

static BOOL
MofHasQualifier(const MOF_QUALIFIER_LIST *List, const WCHAR *Name)
{
    ULONG Index;

    for (Index = 0; Index < List->Count; Index++)
    {
        if (!_wcsicmp(List->Items[Index].Name, Name) &&
            !(List->Items[Index].Value.Kind == MofValueBoolean && !List->Items[Index].Value.Boolean))
            return TRUE;
    }
    return FALSE;
}

static HRESULT
MofPutParameter(MOF_STORE *Store, IWbemClassObject *Signature, const MOF_PROPERTY *Parameter, LONG Id)
{
    IWbemQualifierSet *Set;
    VARIANT IdValue;
    HRESULT Result;

    Result = IWbemClassObject_Put(Signature, Parameter->Name, 0, NULL, Parameter->Type);
    if (FAILED(Result))
        return Result;
    Result = IWbemClassObject_GetPropertyQualifierSet(Signature, Parameter->Name, &Set);
    if (FAILED(Result))
        return Result;
    Result = MofPutQualifiers(Store, Set, &Parameter->Qualifiers);
    if (SUCCEEDED(Result))
    {
        V_VT(&IdValue) = VT_I4;
        V_I4(&IdValue) = Id;
        Result = IWbemQualifierSet_Put(Set, L"ID", &IdValue, 0);
    }
    IWbemQualifierSet_Release(Set);
    return Result;
}

static HRESULT
MofPutMethod(MOF_STORE *Store, IWbemClassObject *Class, IWbemClassObject *Parameters, const MOF_METHOD *Method)
{
    IWbemClassObject *In = NULL, *Out = NULL;
    IWbemQualifierSet *Set;
    ULONG Index;
    LONG Id = 0;
    HRESULT Result = S_OK;

    for (Index = 0; Index < Method->ParameterCount && SUCCEEDED(Result); Index++)
    {
        const MOF_PROPERTY *Parameter = &Method->Parameters[Index];
        BOOL IsIn = MofHasQualifier(&Parameter->Qualifiers, L"In");
        BOOL IsOut = MofHasQualifier(&Parameter->Qualifiers, L"Out");

        if (!IsIn && !IsOut)
        {
            Result = WBEMMOF_E_MUST_BE_IN_OR_OUT;
            break;
        }
        if (IsIn && !In)
            Result = IWbemClassObject_Clone(Parameters, &In);
        if (SUCCEEDED(Result) && IsOut && !Out)
            Result = IWbemClassObject_Clone(Parameters, &Out);
        if (SUCCEEDED(Result) && IsIn)
            Result = MofPutParameter(Store, In, Parameter, Id);
        if (SUCCEEDED(Result) && IsOut)
            Result = MofPutParameter(Store, Out, Parameter, Id);
        Id++;
    }
    if (SUCCEEDED(Result) && Method->ReturnType != CIM_EMPTY)
    {
        if (Method->ReturnType & CIM_FLAG_ARRAY)
            Result = WBEMMOF_E_NO_ARRAYS_RETURNED;
        if (SUCCEEDED(Result) && !Out)
            Result = IWbemClassObject_Clone(Parameters, &Out);
        if (SUCCEEDED(Result))
            Result = IWbemClassObject_Put(Out, L"ReturnValue", 0, NULL, Method->ReturnType);
    }
    if (SUCCEEDED(Result))
        Result = IWbemClassObject_PutMethod(Class, Method->Name, 0, In, Out);
    if (SUCCEEDED(Result) && Method->Qualifiers.Count)
    {
        Result = IWbemClassObject_GetMethodQualifierSet(Class, Method->Name, &Set);
        if (SUCCEEDED(Result))
        {
            Result = MofPutQualifiers(Store, Set, &Method->Qualifiers);
            IWbemQualifierSet_Release(Set);
        }
    }
    if (In)
        IWbemClassObject_Release(In);
    if (Out)
        IWbemClassObject_Release(Out);
    return Result;
}

static HRESULT
MofStoreClass(MOF_STORE *Store, const MOF_OBJECT *Object)
{
    IWbemClassObject *Class = NULL, *Base = NULL, *Parameters = NULL;
    BSTR Name;
    VARIANT Value;
    ULONG Index;
    HRESULT Result;

    if (Object->Super)
    {
        Name = SysAllocString(Object->Super);
        if (!Name)
            return WBEM_E_OUT_OF_MEMORY;
        Result = IWbemServices_GetObject(Store->Services, Name, 0, NULL, &Base, NULL);
        SysFreeString(Name);
        if (SUCCEEDED(Result))
            Result = IWbemClassObject_SpawnDerivedClass(Base, 0, &Class);
    }
    else
    {
        Result = IWbemServices_GetObject(Store->Services, NULL, 0, NULL, &Class, NULL);
    }
    if (SUCCEEDED(Result))
    {
        V_VT(&Value) = VT_BSTR;
        V_BSTR(&Value) = SysAllocString(Object->Name);
        Result = V_BSTR(&Value) ? IWbemClassObject_Put(Class, L"__CLASS", 0, &Value, 0) : WBEM_E_OUT_OF_MEMORY;
        VariantClear(&Value);
    }
    if (SUCCEEDED(Result))
        Result = MofPutObjectQualifiers(Store, Class, &Object->Qualifiers);
    for (Index = 0; Index < Object->PropertyCount && SUCCEEDED(Result); Index++)
    {
        const MOF_PROPERTY *Property = &Object->Properties[Index];

        if (Property->Value.Kind != MofValueNone)
        {
            Result = MofToVariant(Store, &Property->Value, Property->Type, &Value);
            if (SUCCEEDED(Result))
            {
                Result = IWbemClassObject_Put(Class, Property->Name, 0, &Value, Property->Type);
                VariantClear(&Value);
            }
        }
        else
        {
            Result = IWbemClassObject_Put(Class, Property->Name, 0, NULL, Property->Type);
        }
        if (SUCCEEDED(Result))
            Result = MofPutPropertyQualifiers(Store, Class, Property);
    }
    if (SUCCEEDED(Result) && Object->MethodCount)
    {
        Name = SysAllocString(L"__PARAMETERS");
        Result = Name ? IWbemServices_GetObject(Store->Services, Name, 0, NULL, &Parameters, NULL) : WBEM_E_OUT_OF_MEMORY;
        SysFreeString(Name);
    }
    for (Index = 0; Index < Object->MethodCount && SUCCEEDED(Result); Index++)
        Result = MofPutMethod(Store, Class, Parameters, &Object->Methods[Index]);
    if (SUCCEEDED(Result))
        Result = IWbemServices_PutClass(Store->Services, Class, Store->ClassFlags, NULL, NULL);
    if (Parameters)
        IWbemClassObject_Release(Parameters);
    if (Class)
        IWbemClassObject_Release(Class);
    if (Base)
        IWbemClassObject_Release(Base);
    return Result;
}

static HRESULT
MofStoreInstance(MOF_STORE *Store, const MOF_OBJECT *Object)
{
    IWbemClassObject *Class = NULL, *Instance = NULL;
    IWbemCallResult *CallResult = NULL;
    BSTR Name;
    ULONG Index;
    HRESULT Result;

    Name = SysAllocString(Object->Name);
    if (!Name)
        return WBEM_E_OUT_OF_MEMORY;
    Result = IWbemServices_GetObject(Store->Services, Name, 0, NULL, &Class, NULL);
    SysFreeString(Name);
    if (SUCCEEDED(Result))
        Result = IWbemClassObject_SpawnInstance(Class, 0, &Instance);
    if (SUCCEEDED(Result))
        Result = MofPutObjectQualifiers(Store, Instance, &Object->Qualifiers);
    for (Index = 0; Index < Object->PropertyCount && SUCCEEDED(Result); Index++)
    {
        const MOF_PROPERTY *Property = &Object->Properties[Index];
        CIMTYPE Type;
        VARIANT Value;

        Result = IWbemClassObject_Get(Class, Property->Name, 0, NULL, &Type, NULL);
        if (SUCCEEDED(Result))
            Result = MofToVariant(Store, &Property->Value, Type, &Value);
        if (SUCCEEDED(Result))
        {
            Result = IWbemClassObject_Put(Instance, Property->Name, 0, &Value, 0);
            VariantClear(&Value);
        }
        if (SUCCEEDED(Result))
            Result = MofPutPropertyQualifiers(Store, Instance, Property);
    }
    if (SUCCEEDED(Result))
        Result = IWbemServices_PutInstance(Store->Services, Instance, Store->InstanceFlags, NULL,
                                           Object->Alias ? &CallResult : NULL);
    if (SUCCEEDED(Result) && Object->Alias)
    {
        MOF_ALIAS *Alias = MofAlloc(Store->Unit, sizeof(*Alias));
        BSTR Path = NULL;

        if (CallResult)
            Result = IWbemCallResult_GetResultString(CallResult, WBEM_INFINITE, &Path);
        else
            Result = WBEM_E_FAILED;
        if (FAILED(Result))
        {
            VARIANT RelPath;

            Result = IWbemClassObject_Get(Instance, L"__RELPATH", 0, &RelPath, NULL, NULL);
            if (SUCCEEDED(Result) && V_VT(&RelPath) == VT_BSTR)
                Path = V_BSTR(&RelPath);
            else if (SUCCEEDED(Result))
            {
                VariantClear(&RelPath);
                Result = WBEM_E_FAILED;
            }
        }
        if (SUCCEEDED(Result) && !Alias)
        {
            SysFreeString(Path);
            Result = WBEM_E_OUT_OF_MEMORY;
        }
        if (SUCCEEDED(Result))
        {
            Alias->Name = Object->Alias;
            Alias->Path = Path;
            Alias->Next = Store->Aliases;
            Store->Aliases = Alias;
        }
    }
    if (CallResult)
        IWbemCallResult_Release(CallResult);
    if (Instance)
        IWbemClassObject_Release(Instance);
    if (Class)
        IWbemClassObject_Release(Class);
    return Result;
}

static HRESULT
MofStorePragma(MOF_STORE *Store, const MOF_OBJECT *Object)
{
    BSTR Argument;
    HRESULT Result;

    switch (Object->Pragma)
    {
    case MofPragmaNamespace:
        return MofSetNamespace(Store, Object->PragmaArgument);
    case MofPragmaClassFlags:
        Store->ClassFlags = Object->PragmaFlags;
        return S_OK;
    case MofPragmaInstanceFlags:
        Store->InstanceFlags = Object->PragmaFlags;
        return S_OK;
    case MofPragmaDeleteInstance:
    case MofPragmaDeleteClass:
        Result = MofConnect(Store);
        if (SUCCEEDED(Result))
        {
            Argument = SysAllocString(Object->PragmaArgument);
            if (!Argument)
                return WBEM_E_OUT_OF_MEMORY;
            if (Object->Pragma == MofPragmaDeleteInstance)
                Result = IWbemServices_DeleteInstance(Store->Services, Argument, 0, NULL, NULL);
            else
                Result = IWbemServices_DeleteClass(Store->Services, Argument, 0, NULL, NULL);
            SysFreeString(Argument);
        }
        return (FAILED(Result) && !Object->PragmaFail) ? S_OK : Result;
    default:
        return S_OK;
    }
}

HRESULT
MofStore(MOF_UNIT *Unit, const WCHAR *DefaultNamespace, LONG ClassFlags, LONG InstanceFlags,
         WBEM_COMPILE_STATUS_INFO *Info)
{
    MOF_STORE Store;
    MOF_OBJECT *Object;
    HRESULT Result = S_OK;

    ZeroMemory(&Store, sizeof(Store));
    Store.Unit = Unit;
    Store.ClassFlags = ClassFlags;
    Store.InstanceFlags = InstanceFlags;
    Store.Namespace = MofStrDup(Unit, DefaultNamespace, wcslen(DefaultNamespace));
    if (!Store.Namespace)
        return WBEM_E_OUT_OF_MEMORY;
    for (Object = Unit->First; Object && SUCCEEDED(Result); Object = Object->Next)
    {
        BOOL ObjectError = FALSE;

        if (Object->Kind == MofObjectPragma)
        {
            Result = MofStorePragma(&Store, Object);
        }
        else
        {
            Result = MofConnect(&Store);
            if (SUCCEEDED(Result))
            {
                Result = Object->Kind == MofObjectClass ? MofStoreClass(&Store, Object) : MofStoreInstance(&Store, Object);
                ObjectError = TRUE;
            }
        }
        if (FAILED(Result))
        {
            Info->lPhaseError = 3;
            Info->hRes = Result;
            if (ObjectError)
            {
                Info->ObjectNum = Object->Number;
                Info->FirstLine = Object->FirstLine;
                Info->LastLine = Object->LastLine ? Object->LastLine : Object->FirstLine;
            }
        }
    }
    while (Store.Aliases)
    {
        SysFreeString(Store.Aliases->Path);
        Store.Aliases = Store.Aliases->Next;
    }
    if (Store.Services)
        IWbemServices_Release(Store.Services);
    if (Store.Locator)
        IWbemLocator_Release(Store.Locator);
    return Result;
}
