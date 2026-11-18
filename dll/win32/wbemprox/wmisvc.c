/*
 * PROJECT:     LiberNT WMI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IWbemServices of the repository namespaces
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

typedef struct _WMI_ENUM
{
    IEnumWbemClassObject IEnumWbemClassObject_iface;
    LONG Refs;
    ULONG Count;
    ULONG Position;
    IWbemClassObject **Objects;
} WMI_ENUM;

typedef struct _WMI_CALL_RESULT
{
    IWbemCallResult IWbemCallResult_iface;
    LONG Refs;
    HRESULT Status;
    IWbemClassObject *Object;
    WCHAR *Path;
} WMI_CALL_RESULT;

typedef struct _WMI_SERVICES
{
    IWbemServices IWbemServices_iface;
    LONG Refs;
    WCHAR *Namespace;
} WMI_SERVICES;

static const IEnumWbemClassObjectVtbl WmiEnumVtbl;
static const IWbemCallResultVtbl WmiCallResultVtbl;
static const IWbemServicesVtbl WmiServicesVtbl;

/* IEnumWbemClassObject */

static inline WMI_ENUM *
WmiEnumFromIface(IEnumWbemClassObject *Iface)
{
    return CONTAINING_RECORD(Iface, WMI_ENUM, IEnumWbemClassObject_iface);
}

HRESULT
WmiEnumCreate(ULONG Count, IWbemClassObject **Objects, IEnumWbemClassObject **Result)
{
    WMI_ENUM *Enum = calloc(1, sizeof(*Enum));

    if (!Enum)
    {
        WmiFreeObjects(Count, Objects);
        return WBEM_E_OUT_OF_MEMORY;
    }
    Enum->IEnumWbemClassObject_iface.lpVtbl = &WmiEnumVtbl;
    Enum->Refs = 1;
    Enum->Count = Count;
    Enum->Objects = Objects;
    *Result = &Enum->IEnumWbemClassObject_iface;
    return S_OK;
}

static HRESULT WINAPI
WmiEnum_QueryInterface(IEnumWbemClassObject *Iface, REFIID Iid, void **Result)
{
    if (!Result)
        return E_POINTER;
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IEnumWbemClassObject))
    {
        *Result = Iface;
        IEnumWbemClassObject_AddRef(Iface);
        return S_OK;
    }
    *Result = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
WmiEnum_AddRef(IEnumWbemClassObject *Iface)
{
    return InterlockedIncrement(&WmiEnumFromIface(Iface)->Refs);
}

static ULONG WINAPI
WmiEnum_Release(IEnumWbemClassObject *Iface)
{
    WMI_ENUM *Enum = WmiEnumFromIface(Iface);
    LONG Refs = InterlockedDecrement(&Enum->Refs);

    if (!Refs)
    {
        WmiFreeObjects(Enum->Count, Enum->Objects);
        free(Enum);
    }
    return Refs;
}

static HRESULT WINAPI
WmiEnum_Reset(IEnumWbemClassObject *Iface)
{
    WmiEnumFromIface(Iface)->Position = 0;
    return WBEM_S_NO_ERROR;
}

static HRESULT WINAPI
WmiEnum_Next(IEnumWbemClassObject *Iface, LONG Timeout, ULONG Count, IWbemClassObject **Objects, ULONG *Returned)
{
    WMI_ENUM *Enum = WmiEnumFromIface(Iface);
    ULONG Index;

    if (!Objects || !Returned)
        return WBEM_E_INVALID_PARAMETER;
    if (Timeout != WBEM_INFINITE && Timeout < 0)
        return WBEM_E_INVALID_PARAMETER;
    for (Index = 0; Index < Count && Enum->Position < Enum->Count; Index++)
    {
        HRESULT hr = IWbemClassObject_Clone(Enum->Objects[Enum->Position], &Objects[Index]);

        if (FAILED(hr))
        {
            while (Index--)
                IWbemClassObject_Release(Objects[Index]);
            *Returned = 0;
            return hr;
        }
        Enum->Position++;
    }
    *Returned = Index;
    return Index == Count ? WBEM_S_NO_ERROR : WBEM_S_FALSE;
}

static HRESULT WINAPI
WmiEnum_NextAsync(IEnumWbemClassObject *Iface, ULONG Count, IWbemObjectSink *Sink)
{
    /* TODO: semisynchronous delivery of the remaining objects to a sink */
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI
WmiEnum_Clone(IEnumWbemClassObject *Iface, IEnumWbemClassObject **Result)
{
    WMI_ENUM *Enum = WmiEnumFromIface(Iface);
    IWbemClassObject **Copies = NULL;
    ULONG Index;
    HRESULT hr = S_OK;

    if (!Result)
        return WBEM_E_INVALID_PARAMETER;
    if (Enum->Count && !(Copies = calloc(Enum->Count, sizeof(*Copies))))
        return WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; SUCCEEDED(hr) && Index < Enum->Count; Index++)
        hr = IWbemClassObject_Clone(Enum->Objects[Index], &Copies[Index]);
    if (FAILED(hr))
    {
        WmiFreeObjects(Enum->Count, Copies);
        return hr;
    }
    hr = WmiEnumCreate(Enum->Count, Copies, Result);
    if (SUCCEEDED(hr))
        WmiEnumFromIface(*Result)->Position = Enum->Position;
    return hr;
}

static HRESULT WINAPI
WmiEnum_Skip(IEnumWbemClassObject *Iface, LONG Timeout, ULONG Count)
{
    WMI_ENUM *Enum = WmiEnumFromIface(Iface);
    ULONG Left = Enum->Count - Enum->Position;

    Enum->Position += min(Count, Left);
    return Count <= Left ? WBEM_S_NO_ERROR : WBEM_S_FALSE;
}

static const IEnumWbemClassObjectVtbl WmiEnumVtbl =
{
    WmiEnum_QueryInterface, WmiEnum_AddRef, WmiEnum_Release, WmiEnum_Reset, WmiEnum_Next, WmiEnum_NextAsync,
    WmiEnum_Clone, WmiEnum_Skip
};

/* IWbemCallResult of a synchronous call */

static inline WMI_CALL_RESULT *
WmiCallResultFromIface(IWbemCallResult *Iface)
{
    return CONTAINING_RECORD(Iface, WMI_CALL_RESULT, IWbemCallResult_iface);
}

HRESULT
WmiCallResultCreate(HRESULT Status, IWbemClassObject *Object, const WCHAR *Path, IWbemCallResult **Result)
{
    WMI_CALL_RESULT *Call = calloc(1, sizeof(*Call));

    if (!Call)
        return WBEM_E_OUT_OF_MEMORY;
    Call->IWbemCallResult_iface.lpVtbl = &WmiCallResultVtbl;
    Call->Refs = 1;
    Call->Status = Status;
    Call->Object = Object;
    if (Object)
        IWbemClassObject_AddRef(Object);
    if (Path && !(Call->Path = _wcsdup(Path)))
    {
        if (Object)
            IWbemClassObject_Release(Object);
        free(Call);
        return WBEM_E_OUT_OF_MEMORY;
    }
    *Result = &Call->IWbemCallResult_iface;
    return S_OK;
}

static HRESULT WINAPI
WmiCallResult_QueryInterface(IWbemCallResult *Iface, REFIID Iid, void **Result)
{
    if (!Result)
        return E_POINTER;
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IWbemCallResult))
    {
        *Result = Iface;
        IWbemCallResult_AddRef(Iface);
        return S_OK;
    }
    *Result = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
WmiCallResult_AddRef(IWbemCallResult *Iface)
{
    return InterlockedIncrement(&WmiCallResultFromIface(Iface)->Refs);
}

static ULONG WINAPI
WmiCallResult_Release(IWbemCallResult *Iface)
{
    WMI_CALL_RESULT *Call = WmiCallResultFromIface(Iface);
    LONG Refs = InterlockedDecrement(&Call->Refs);

    if (!Refs)
    {
        if (Call->Object)
            IWbemClassObject_Release(Call->Object);
        free(Call->Path);
        free(Call);
    }
    return Refs;
}

static HRESULT WINAPI
WmiCallResult_GetResultObject(IWbemCallResult *Iface, LONG Timeout, IWbemClassObject **Object)
{
    WMI_CALL_RESULT *Call = WmiCallResultFromIface(Iface);

    if (!Object)
        return WBEM_E_INVALID_PARAMETER;
    if (FAILED(Call->Status))
        return Call->Status;
    if (!Call->Object)
        return WBEM_E_NOT_FOUND;
    return IWbemClassObject_Clone(Call->Object, Object);
}

static HRESULT WINAPI
WmiCallResult_GetResultString(IWbemCallResult *Iface, LONG Timeout, BSTR *Result)
{
    WMI_CALL_RESULT *Call = WmiCallResultFromIface(Iface);

    if (!Result)
        return WBEM_E_INVALID_PARAMETER;
    if (FAILED(Call->Status))
        return Call->Status;
    if (!Call->Path)
        return WBEM_E_INVALID_OPERATION;
    *Result = SysAllocString(Call->Path);
    return *Result ? S_OK : WBEM_E_OUT_OF_MEMORY;
}

static HRESULT WINAPI
WmiCallResult_GetResultServices(IWbemCallResult *Iface, LONG Timeout, IWbemServices **Services)
{
    return WBEM_E_INVALID_OPERATION;
}

static HRESULT WINAPI
WmiCallResult_GetCallStatus(IWbemCallResult *Iface, LONG Timeout, LONG *Status)
{
    if (!Status)
        return WBEM_E_INVALID_PARAMETER;
    *Status = WmiCallResultFromIface(Iface)->Status;
    return WBEM_S_NO_ERROR;
}

static const IWbemCallResultVtbl WmiCallResultVtbl =
{
    WmiCallResult_QueryInterface, WmiCallResult_AddRef, WmiCallResult_Release, WmiCallResult_GetResultObject,
    WmiCallResult_GetResultString, WmiCallResult_GetResultServices, WmiCallResult_GetCallStatus
};

/* IWbemServices */

static inline WMI_SERVICES *
WmiServicesFromIface(IWbemServices *Iface)
{
    return CONTAINING_RECORD(Iface, WMI_SERVICES, IWbemServices_iface);
}

HRESULT
WmiRepoServicesCreate(const WCHAR *Namespace, IWbemServices **Result)
{
    WMI_SERVICES *Services;

    if (!WmiRepoNamespaceExists(Namespace))
        return WBEM_E_INVALID_NAMESPACE;
    if (!(Services = calloc(1, sizeof(*Services))) || !(Services->Namespace = _wcsdup(Namespace ? Namespace : L"")))
    {
        free(Services);
        return WBEM_E_OUT_OF_MEMORY;
    }
    Services->IWbemServices_iface.lpVtbl = &WmiServicesVtbl;
    Services->Refs = 1;
    *Result = &Services->IWbemServices_iface;
    return S_OK;
}

static HRESULT WINAPI
WmiServices_QueryInterface(IWbemServices *Iface, REFIID Iid, void **Result)
{
    if (!Result)
        return E_POINTER;
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IWbemServices))
    {
        *Result = Iface;
        IWbemServices_AddRef(Iface);
        return S_OK;
    }
    *Result = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
WmiServices_AddRef(IWbemServices *Iface)
{
    return InterlockedIncrement(&WmiServicesFromIface(Iface)->Refs);
}

static ULONG WINAPI
WmiServices_Release(IWbemServices *Iface)
{
    WMI_SERVICES *Services = WmiServicesFromIface(Iface);
    LONG Refs = InterlockedDecrement(&Services->Refs);

    if (!Refs)
    {
        free(Services->Namespace);
        free(Services);
    }
    return Refs;
}

static HRESULT
WmiCallResultFor(HRESULT Status, IWbemClassObject *Object, const WCHAR *Path, IWbemCallResult **CallResult)
{
    if (!CallResult)
        return Status;
    *CallResult = NULL;
    if (FAILED(WmiCallResultCreate(Status, Object, Path, CallResult)))
        return WBEM_E_OUT_OF_MEMORY;
    return Status;
}

static HRESULT WINAPI
WmiServices_OpenNamespace(IWbemServices *Iface, const BSTR Name, LONG Flags, IWbemContext *Context,
                          IWbemServices **Result, IWbemCallResult **CallResult)
{
    WMI_SERVICES *Services = WmiServicesFromIface(Iface);
    WCHAR *Child;
    SIZE_T Length;
    HRESULT hr;

    if (!Name || !Result)
        return WBEM_E_INVALID_PARAMETER;
    Length = wcslen(Services->Namespace) + wcslen(Name) + 2;
    if (!(Child = malloc(Length * sizeof(WCHAR))))
        return WBEM_E_OUT_OF_MEMORY;
    swprintf(Child, Length, *Services->Namespace ? L"%ls\\%ls" : L"%ls%ls", Services->Namespace, Name);
    hr = WmiRepoServicesCreate(Child, Result);
    free(Child);
    return hr;
}

static HRESULT WINAPI
WmiServices_CancelAsyncCall(IWbemServices *Iface, IWbemObjectSink *Sink)
{
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI
WmiServices_QueryObjectSink(IWbemServices *Iface, LONG Flags, IWbemObjectSink **Sink)
{
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT
WmiNamespaceInstance(WMI_SERVICES *Services, const WCHAR *Name, IWbemClassObject **Result)
{
    IWbemClassObject *Class;
    VARIANT Value;
    HRESULT hr = WmiRepoSystemClass(Services->Namespace, L"__NAMESPACE", &Class);

    if (FAILED(hr))
        return hr;
    hr = IWbemClassObject_SpawnInstance(Class, 0, Result);
    IWbemClassObject_Release(Class);
    if (FAILED(hr))
        return hr;
    V_VT(&Value) = VT_BSTR;
    V_BSTR(&Value) = SysAllocString(Name);
    hr = IWbemClassObject_Put(*Result, L"Name", 0, &Value, 0);
    VariantClear(&Value);
    if (FAILED(hr))
    {
        IWbemClassObject_Release(*Result);
        *Result = NULL;
    }
    return hr;
}

static HRESULT
WmiNamespaceChildren(WMI_SERVICES *Services, ULONG *Count, IWbemClassObject ***Instances)
{
    WCHAR **Names;
    ULONG NameCount, Index;
    HRESULT hr = WmiRepoEnumNamespaces(Services->Namespace, &NameCount, &Names);

    *Count = 0;
    *Instances = NULL;
    if (FAILED(hr))
        return hr;
    if (NameCount && !(*Instances = calloc(NameCount, sizeof(**Instances))))
        hr = WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; Index < NameCount; Index++)
    {
        if (SUCCEEDED(hr))
            hr = WmiNamespaceInstance(Services, Names[Index], &(*Instances)[(*Count)++]);
        free(Names[Index]);
    }
    free(Names);
    if (FAILED(hr))
    {
        WmiFreeObjects(*Count, *Instances);
        *Count = 0;
        *Instances = NULL;
    }
    return hr;
}

static HRESULT
WmiGetObjectPath(WMI_SERVICES *Services, const WCHAR *Text, IWbemClassObject **Result)
{
    WMI_PATH Path;
    IWbemClassObject *Class = NULL;
    HRESULT hr;

    *Result = NULL;
    if (!Text || !*Text)
    {
        hr = WmiObjectCreateClass(Result);
        return hr;
    }
    hr = WmiParsePath(Text, &Path);
    if (FAILED(hr))
        return hr;
    if (!Path.KeyCount && !Path.Singleton)
        hr = WmiRepoGetClass(Services->Namespace, Path.Class, Result);
    else if (!_wcsicmp(Path.Class, L"__NAMESPACE"))
    {
        hr = WBEM_E_NOT_FOUND;
        if (Path.KeyCount == 1 && V_VT(&Path.KeyValues[0]) == VT_BSTR)
        {
            ULONG Count, Index;
            IWbemClassObject **Children;

            hr = WmiNamespaceChildren(Services, &Count, &Children);
            for (Index = 0; SUCCEEDED(hr) && Index < Count && !*Result; Index++)
                if (WmiPathMatchesInstance(&Path, Children[Index]))
                {
                    *Result = Children[Index];
                    Children[Index] = NULL;
                }
            if (SUCCEEDED(hr))
            {
                WmiFreeObjects(Count, Children);
                if (!*Result)
                    hr = WBEM_E_NOT_FOUND;
            }
        }
    }
    else
    {
        hr = WmiRepoGetClass(Services->Namespace, Path.Class, &Class);
        if (hr == WBEM_E_NOT_FOUND)
            hr = WBEM_E_INVALID_CLASS;
        if (SUCCEEDED(hr))
            hr = WmiClassIsDynamic(Class) ? WmiProviderGetObject(Services->Namespace, Class, Text, Result) :
                                            WmiRepoGetInstance(Services->Namespace, &Path, Result);
    }
    if (Class)
        IWbemClassObject_Release(Class);
    WmiFreePath(&Path);
    return hr;
}

static HRESULT WINAPI
WmiServices_GetObject(IWbemServices *Iface, const BSTR Path, LONG Flags, IWbemContext *Context,
                      IWbemClassObject **Object, IWbemCallResult **CallResult)
{
    IWbemClassObject *Result = NULL;
    HRESULT hr = WmiGetObjectPath(WmiServicesFromIface(Iface), Path, &Result);

    if (Object)
        *Object = Result;
    hr = WmiCallResultFor(hr, Result, NULL, CallResult);
    if (!Object && Result)
        IWbemClassObject_Release(Result);
    return hr;
}

static HRESULT
WmiDeliver(IWbemObjectSink *Sink, HRESULT Status, ULONG Count, IWbemClassObject **Objects)
{
    if (!Sink)
        return WBEM_E_INVALID_PARAMETER;
    if (Count)
        IWbemObjectSink_Indicate(Sink, (LONG)Count, Objects);
    IWbemObjectSink_SetStatus(Sink, WBEM_STATUS_COMPLETE, Status, NULL, NULL);
    return WBEM_S_NO_ERROR;
}

static HRESULT WINAPI
WmiServices_GetObjectAsync(IWbemServices *Iface, const BSTR Path, LONG Flags, IWbemContext *Context,
                           IWbemObjectSink *Sink)
{
    IWbemClassObject *Result = NULL;
    HRESULT hr = WmiGetObjectPath(WmiServicesFromIface(Iface), Path, &Result);

    hr = WmiDeliver(Sink, hr, Result ? 1 : 0, &Result);
    if (Result)
        IWbemClassObject_Release(Result);
    return hr;
}

static HRESULT WINAPI
WmiServices_PutClass(IWbemServices *Iface, IWbemClassObject *Class, LONG Flags, IWbemContext *Context,
                     IWbemCallResult **CallResult)
{
    HRESULT hr = Class ? WmiRepoPutClass(WmiServicesFromIface(Iface)->Namespace, Class, Flags) :
                         WBEM_E_INVALID_PARAMETER;

    return WmiCallResultFor(hr, NULL, NULL, CallResult);
}

static HRESULT WINAPI
WmiServices_PutClassAsync(IWbemServices *Iface, IWbemClassObject *Class, LONG Flags, IWbemContext *Context,
                          IWbemObjectSink *Sink)
{
    return WmiDeliver(Sink, WmiServices_PutClass(Iface, Class, Flags, Context, NULL), 0, NULL);
}

static HRESULT WINAPI
WmiServices_DeleteClass(IWbemServices *Iface, const BSTR Class, LONG Flags, IWbemContext *Context,
                        IWbemCallResult **CallResult)
{
    HRESULT hr = Class ? WmiRepoDeleteClass(WmiServicesFromIface(Iface)->Namespace, Class) : WBEM_E_INVALID_PARAMETER;

    return WmiCallResultFor(hr, NULL, NULL, CallResult);
}

static HRESULT WINAPI
WmiServices_DeleteClassAsync(IWbemServices *Iface, const BSTR Class, LONG Flags, IWbemContext *Context,
                             IWbemObjectSink *Sink)
{
    return WmiDeliver(Sink, WmiServices_DeleteClass(Iface, Class, Flags, Context, NULL), 0, NULL);
}

static HRESULT WINAPI
WmiServices_CreateClassEnum(IWbemServices *Iface, const BSTR Superclass, LONG Flags, IWbemContext *Context,
                            IEnumWbemClassObject **Enum)
{
    IWbemClassObject **Classes;
    ULONG Count;
    HRESULT hr;

    if (!Enum)
        return WBEM_E_INVALID_PARAMETER;
    hr = WmiRepoEnumClasses(WmiServicesFromIface(Iface)->Namespace, Superclass, !(Flags & WBEM_FLAG_SHALLOW), &Count,
                            &Classes);
    if (FAILED(hr))
        return hr;
    return WmiEnumCreate(Count, Classes, Enum);
}

static HRESULT WINAPI
WmiServices_CreateClassEnumAsync(IWbemServices *Iface, const BSTR Superclass, LONG Flags, IWbemContext *Context,
                                 IWbemObjectSink *Sink)
{
    IWbemClassObject **Classes = NULL;
    ULONG Count = 0;
    HRESULT hr = WmiRepoEnumClasses(WmiServicesFromIface(Iface)->Namespace, Superclass, !(Flags & WBEM_FLAG_SHALLOW),
                                    &Count, &Classes);

    hr = WmiDeliver(Sink, hr, Count, Classes);
    WmiFreeObjects(Count, Classes);
    return hr;
}

static HRESULT
WmiPutInstance(WMI_SERVICES *Services, IWbemClassObject *Instance, LONG Flags, WCHAR **Path)
{
    *Path = NULL;
    if (!Instance)
        return WBEM_E_INVALID_PARAMETER;
    if (!WmiObjectIsOurs(Instance) || WmiObjectGenus(Instance) != WBEM_GENUS_INSTANCE)
        return WBEM_E_INVALID_PARAMETER;
    if (!_wcsicmp(WmiObjectClassName(Instance), L"__NAMESPACE"))
    {
        VARIANT Name;
        HRESULT hr;

        VariantInit(&Name);
        if (FAILED(IWbemClassObject_Get(Instance, L"Name", 0, &Name, NULL, NULL)) || V_VT(&Name) != VT_BSTR)
        {
            VariantClear(&Name);
            return WBEM_E_ILLEGAL_NULL;
        }
        hr = WmiRepoCreateNamespace(Services->Namespace, V_BSTR(&Name));
        if (hr == WBEM_E_ALREADY_EXISTS && !(Flags & WBEM_FLAG_CREATE_ONLY))
            hr = S_OK;
        if (SUCCEEDED(hr))
            WmiObjectRelativePath(Instance, Path);
        VariantClear(&Name);
        return hr;
    }
    /* TODO: instances of dynamic classes go to their provider's PutInstanceAsync */
    return WmiRepoPutInstance(Services->Namespace, Instance, Flags, Path);
}

static HRESULT WINAPI
WmiServices_PutInstance(IWbemServices *Iface, IWbemClassObject *Instance, LONG Flags, IWbemContext *Context,
                        IWbemCallResult **CallResult)
{
    WCHAR *Path;
    HRESULT hr = WmiPutInstance(WmiServicesFromIface(Iface), Instance, Flags, &Path);

    hr = WmiCallResultFor(hr, NULL, Path, CallResult);
    free(Path);
    return hr;
}

static HRESULT WINAPI
WmiServices_PutInstanceAsync(IWbemServices *Iface, IWbemClassObject *Instance, LONG Flags, IWbemContext *Context,
                             IWbemObjectSink *Sink)
{
    return WmiDeliver(Sink, WmiServices_PutInstance(Iface, Instance, Flags, Context, NULL), 0, NULL);
}

static HRESULT
WmiDeleteInstance(WMI_SERVICES *Services, const WCHAR *Text)
{
    WMI_PATH Path;
    HRESULT hr = WmiParsePath(Text, &Path);

    if (FAILED(hr))
        return hr;
    if (!Path.KeyCount && !Path.Singleton)
        hr = WBEM_E_INVALID_OBJECT_PATH;
    else if (!_wcsicmp(Path.Class, L"__NAMESPACE"))
        hr = (Path.KeyCount == 1 && V_VT(&Path.KeyValues[0]) == VT_BSTR) ?
             WmiRepoDeleteNamespace(Services->Namespace, V_BSTR(&Path.KeyValues[0])) : WBEM_E_INVALID_OBJECT_PATH;
    else
        /* TODO: instances of dynamic classes go to their provider's DeleteInstanceAsync */
        hr = WmiRepoDeleteInstance(Services->Namespace, &Path);
    WmiFreePath(&Path);
    return hr;
}

static HRESULT WINAPI
WmiServices_DeleteInstance(IWbemServices *Iface, const BSTR Path, LONG Flags, IWbemContext *Context,
                           IWbemCallResult **CallResult)
{
    HRESULT hr = Path ? WmiDeleteInstance(WmiServicesFromIface(Iface), Path) : WBEM_E_INVALID_PARAMETER;

    return WmiCallResultFor(hr, NULL, NULL, CallResult);
}

static HRESULT WINAPI
WmiServices_DeleteInstanceAsync(IWbemServices *Iface, const BSTR Path, LONG Flags, IWbemContext *Context,
                                IWbemObjectSink *Sink)
{
    return WmiDeliver(Sink, WmiServices_DeleteInstance(Iface, Path, Flags, Context, NULL), 0, NULL);
}

static HRESULT
WmiInstances(WMI_SERVICES *Services, const WCHAR *ClassName, BOOL Deep, ULONG *Count, IWbemClassObject ***Instances)
{
    IWbemClassObject *Class;
    HRESULT hr;

    *Count = 0;
    *Instances = NULL;
    if (!ClassName)
        return WBEM_E_INVALID_PARAMETER;
    if (!_wcsicmp(ClassName, L"__NAMESPACE"))
        return WmiNamespaceChildren(Services, Count, Instances);
    hr = WmiRepoGetClass(Services->Namespace, ClassName, &Class);
    if (FAILED(hr))
        return hr == WBEM_E_NOT_FOUND ? WBEM_E_INVALID_CLASS : hr;
    /* TODO: a deep enumeration also asks the providers of dynamic subclasses */
    if (WmiClassIsDynamic(Class))
    {
        hr = WmiProviderEnumerate(Services->Namespace, Class, Count, Instances);
        /* A provider that finds no instance gives an empty enumeration */
        if (hr == WBEM_E_NOT_FOUND)
        {
            WmiFreeObjects(*Count, *Instances);
            *Count = 0;
            *Instances = NULL;
            hr = S_OK;
        }
    }
    else
        hr = WmiRepoEnumInstances(Services->Namespace, ClassName, Deep, Count, Instances);
    IWbemClassObject_Release(Class);
    return hr;
}

static HRESULT WINAPI
WmiServices_CreateInstanceEnum(IWbemServices *Iface, const BSTR Class, LONG Flags, IWbemContext *Context,
                               IEnumWbemClassObject **Enum)
{
    IWbemClassObject **Instances;
    ULONG Count;
    HRESULT hr;

    if (!Enum)
        return WBEM_E_INVALID_PARAMETER;
    hr = WmiInstances(WmiServicesFromIface(Iface), Class, !(Flags & WBEM_FLAG_SHALLOW), &Count, &Instances);
    if (FAILED(hr))
        return hr;
    return WmiEnumCreate(Count, Instances, Enum);
}

static HRESULT WINAPI
WmiServices_CreateInstanceEnumAsync(IWbemServices *Iface, const BSTR Class, LONG Flags, IWbemContext *Context,
                                    IWbemObjectSink *Sink)
{
    IWbemClassObject **Instances = NULL;
    ULONG Count = 0;
    HRESULT hr = WmiInstances(WmiServicesFromIface(Iface), Class, !(Flags & WBEM_FLAG_SHALLOW), &Count, &Instances);

    hr = WmiDeliver(Sink, hr, Count, Instances);
    WmiFreeObjects(Count, Instances);
    return hr;
}

static HRESULT
WmiQuery(WMI_SERVICES *Services, const WCHAR *Language, const WCHAR *Text, ULONG *Count,
         IWbemClassObject ***Results)
{
    WMI_QUERY *Query;
    IWbemClassObject **Instances;
    ULONG Total, Index;
    HRESULT hr;

    *Count = 0;
    *Results = NULL;
    if (!Language || !Text)
        return WBEM_E_INVALID_PARAMETER;
    if (_wcsicmp(Language, L"WQL"))
        return WBEM_E_INVALID_QUERY_TYPE;
    hr = WmiParseQuery(Text, &Query);
    if (FAILED(hr))
        return hr;
    hr = WmiInstances(Services, WmiQueryClass(Query), TRUE, &Total, &Instances);
    if (SUCCEEDED(hr))
    {
        for (Index = 0; Index < Total; Index++)
        {
            if (!WmiObjectMatches(Instances[Index], Query))
            {
                IWbemClassObject_Release(Instances[Index]);
                continue;
            }
            Instances[(*Count)++] = Instances[Index];
        }
        *Results = Instances;
    }
    WmiFreeQuery(Query);
    return hr;
}

static HRESULT WINAPI
WmiServices_ExecQuery(IWbemServices *Iface, const BSTR Language, const BSTR Query, LONG Flags, IWbemContext *Context,
                      IEnumWbemClassObject **Enum)
{
    IWbemClassObject **Results;
    ULONG Count;
    HRESULT hr;

    if (!Enum)
        return WBEM_E_INVALID_PARAMETER;
    hr = WmiQuery(WmiServicesFromIface(Iface), Language, Query, &Count, &Results);
    if (FAILED(hr))
        return hr;
    return WmiEnumCreate(Count, Results, Enum);
}

static HRESULT WINAPI
WmiServices_ExecQueryAsync(IWbemServices *Iface, const BSTR Language, const BSTR Query, LONG Flags,
                           IWbemContext *Context, IWbemObjectSink *Sink)
{
    IWbemClassObject **Results = NULL;
    ULONG Count = 0;
    HRESULT hr = WmiQuery(WmiServicesFromIface(Iface), Language, Query, &Count, &Results);

    hr = WmiDeliver(Sink, hr, Count, Results);
    WmiFreeObjects(Count, Results);
    return hr;
}

static HRESULT WINAPI
WmiServices_ExecNotificationQuery(IWbemServices *Iface, const BSTR Language, const BSTR Query, LONG Flags,
                                  IWbemContext *Context, IEnumWbemClassObject **Enum)
{
    /* TODO: event queries */
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI
WmiServices_ExecNotificationQueryAsync(IWbemServices *Iface, const BSTR Language, const BSTR Query, LONG Flags,
                                       IWbemContext *Context, IWbemObjectSink *Sink)
{
    /* TODO: event queries */
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT
WmiExecMethod(WMI_SERVICES *Services, const WCHAR *Text, const WCHAR *Method, IWbemClassObject *InParams,
              IWbemClassObject **OutParams)
{
    WMI_PATH Path;
    IWbemClassObject *Class = NULL, *Instance = NULL;
    HRESULT hr;

    *OutParams = NULL;
    if (!Text || !Method)
        return WBEM_E_INVALID_PARAMETER;
    hr = WmiParsePath(Text, &Path);
    if (FAILED(hr))
        return hr;
    hr = WmiRepoGetClass(Services->Namespace, Path.Class, &Class);
    if (hr == WBEM_E_NOT_FOUND)
        hr = WBEM_E_INVALID_CLASS;
    if (SUCCEEDED(hr) && FAILED(IWbemClassObject_GetMethod(Class, Method, 0, NULL, NULL)))
        hr = WBEM_E_INVALID_METHOD;
    if (SUCCEEDED(hr) && !WmiClassIsDynamic(Class))
        /* TODO: methods of static classes are executed by their method provider */
        hr = WBEM_E_INVALID_METHOD;
    if (SUCCEEDED(hr) && (Path.KeyCount || Path.Singleton))
        hr = WmiProviderGetObject(Services->Namespace, Class, Text, &Instance);
    if (SUCCEEDED(hr))
        hr = WmiProviderExecMethod(Services->Namespace, Class, Text, Method, InParams, OutParams);
    if (Instance)
        IWbemClassObject_Release(Instance);
    if (Class)
        IWbemClassObject_Release(Class);
    WmiFreePath(&Path);
    return hr;
}

static HRESULT WINAPI
WmiServices_ExecMethod(IWbemServices *Iface, const BSTR Path, const BSTR Method, LONG Flags, IWbemContext *Context,
                       IWbemClassObject *InParams, IWbemClassObject **OutParams, IWbemCallResult **CallResult)
{
    IWbemClassObject *Out = NULL;
    HRESULT hr = WmiExecMethod(WmiServicesFromIface(Iface), Path, Method, InParams, &Out);

    hr = WmiCallResultFor(hr, Out, NULL, CallResult);
    if (OutParams)
        *OutParams = Out;
    else if (Out)
        IWbemClassObject_Release(Out);
    return hr;
}

static HRESULT WINAPI
WmiServices_ExecMethodAsync(IWbemServices *Iface, const BSTR Path, const BSTR Method, LONG Flags,
                            IWbemContext *Context, IWbemClassObject *InParams, IWbemObjectSink *Sink)
{
    IWbemClassObject *Out = NULL;
    HRESULT hr = WmiExecMethod(WmiServicesFromIface(Iface), Path, Method, InParams, &Out);

    hr = WmiDeliver(Sink, hr, Out ? 1 : 0, &Out);
    if (Out)
        IWbemClassObject_Release(Out);
    return hr;
}

static const IWbemServicesVtbl WmiServicesVtbl =
{
    WmiServices_QueryInterface, WmiServices_AddRef, WmiServices_Release, WmiServices_OpenNamespace,
    WmiServices_CancelAsyncCall, WmiServices_QueryObjectSink, WmiServices_GetObject, WmiServices_GetObjectAsync,
    WmiServices_PutClass, WmiServices_PutClassAsync, WmiServices_DeleteClass, WmiServices_DeleteClassAsync,
    WmiServices_CreateClassEnum, WmiServices_CreateClassEnumAsync, WmiServices_PutInstance,
    WmiServices_PutInstanceAsync, WmiServices_DeleteInstance, WmiServices_DeleteInstanceAsync,
    WmiServices_CreateInstanceEnum, WmiServices_CreateInstanceEnumAsync, WmiServices_ExecQuery,
    WmiServices_ExecQueryAsync, WmiServices_ExecNotificationQuery, WmiServices_ExecNotificationQueryAsync,
    WmiServices_ExecMethod, WmiServices_ExecMethodAsync
};

/* The built-in namespaces keep their own services; system classes and repository classes in them are served here */
static BOOL
WmiRepoOwnsClass(const WCHAR *Namespace, const WCHAR *Class)
{
    IWbemClassObject *Object;

    if (!Class)
        return FALSE;
    if (Class[0] == L'_' && Class[1] == L'_')
        return TRUE;
    if (FAILED(WmiRepoGetClass(Namespace, Class, &Object)))
        return FALSE;
    IWbemClassObject_Release(Object);
    return TRUE;
}

HRESULT
WmiBuiltinNamespaceHook(const WCHAR *Namespace, const WCHAR *Operation, const WCHAR *Text, IWbemClassObject *Object,
                        LONG Flags, IWbemClassObject **Result, BOOL *Handled)
{
    WMI_SERVICES Services = { { &WmiServicesVtbl }, 1, (WCHAR *)(Namespace ? Namespace : L"") };
    WMI_PATH Path;
    WCHAR *RelativePath = NULL;
    const WCHAR *Class = NULL;
    HRESULT hr = S_OK;

    *Handled = FALSE;
    if (Result)
        *Result = NULL;
    if (!_wcsicmp(Operation, L"PutClass"))
    {
        *Handled = TRUE;
        return Object ? WmiRepoPutClass(Services.Namespace, Object, Flags) : WBEM_E_INVALID_PARAMETER;
    }
    if (!_wcsicmp(Operation, L"PutInstance"))
    {
        if (!Object || !WmiObjectIsOurs(Object))
            return S_OK;
        if (!WmiRepoOwnsClass(Services.Namespace, WmiObjectClassName(Object)))
            return S_OK;
        *Handled = TRUE;
        hr = WmiPutInstance(&Services, Object, Flags, &RelativePath);
        free(RelativePath);
        return hr;
    }
    if (!Text)
        return S_OK;
    if (SUCCEEDED(WmiParsePath(Text, &Path)))
    {
        Class = Path.Class;
        *Handled = WmiRepoOwnsClass(Services.Namespace, Class);
        WmiFreePath(&Path);
    }
    else if (!_wcsicmp(Operation, L"ExecQuery") || !_wcsicmp(Operation, L"CreateInstanceEnum"))
    {
        WMI_QUERY *Query;

        if (!_wcsicmp(Operation, L"ExecQuery") && SUCCEEDED(WmiParseQuery(Text, &Query)))
        {
            *Handled = WmiRepoOwnsClass(Services.Namespace, WmiQueryClass(Query));
            WmiFreeQuery(Query);
        }
    }
    if (!*Handled)
        return S_OK;
    if (!_wcsicmp(Operation, L"GetObject"))
        hr = WmiGetObjectPath(&Services, Text, Result);
    else if (!_wcsicmp(Operation, L"DeleteInstance"))
        hr = WmiDeleteInstance(&Services, Text);
    else if (!_wcsicmp(Operation, L"DeleteClass"))
        hr = WmiRepoDeleteClass(Services.Namespace, Text);
    else
        *Handled = FALSE;
    return hr;
}

HRESULT
WmiBuiltinNamespaceEnum(const WCHAR *Namespace, const WCHAR *Operation, const WCHAR *Text, LONG Flags,
                        IEnumWbemClassObject **Enum, BOOL *Handled)
{
    WMI_SERVICES Services = { { &WmiServicesVtbl }, 1, (WCHAR *)(Namespace ? Namespace : L"") };
    WMI_QUERY *Query = NULL;
    const WCHAR *Class = Text;
    HRESULT hr;

    *Handled = FALSE;
    if (!Text)
        return S_OK;
    if (!_wcsicmp(Operation, L"ExecQuery"))
    {
        if (FAILED(WmiParseQuery(Text, &Query)))
            return S_OK;
        Class = WmiQueryClass(Query);
    }
    if (WmiRepoOwnsClass(Services.Namespace, Class))
    {
        BSTR Language = SysAllocString(L"WQL"), String = SysAllocString(Text);

        *Handled = TRUE;
        hr = Query ? WmiServices_ExecQuery(&Services.IWbemServices_iface, Language, String, Flags, NULL, Enum) :
                     WmiServices_CreateInstanceEnum(&Services.IWbemServices_iface, String, Flags, NULL, Enum);
        SysFreeString(Language);
        SysFreeString(String);
    }
    else
        hr = S_OK;
    WmiFreeQuery(Query);
    return hr;
}
