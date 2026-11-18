/*
 * PROJECT:     LiberNT WMI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Decoupled providers: IWbemDecoupledRegistrar and the requests sent to the provider process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <objbase.h>
#include <oleauto.h>
#include <sddl.h>
#include <wchar.h>
#include <assert.h>
#include "wmirepo.h"

#define WMI_DECOUPLED_KEY L"SOFTWARE\\LiberNT\\Wbem\\Decoupled"
#define WMI_PROVIDER_TIMEOUT 60000
#define WMI_PIPE_BUFFER 0x10000
#define WMI_MAX_MESSAGE (64 * 1024 * 1024)

typedef enum _WMI_PROVIDER_OPERATION
{
    WmiOperationEnumerate = 1,
    WmiOperationGetObject = 2,
    WmiOperationExecMethod = 3
} WMI_PROVIDER_OPERATION;

/* A sink that collects what a provider reports and waits for its final status */
typedef struct _WMI_COLLECTOR
{
    IWbemObjectSink IWbemObjectSink_iface;
    IWbemProviderInitSink IWbemProviderInitSink_iface;
    LONG Refs;
    CRITICAL_SECTION Lock;
    HANDLE Done;
    HRESULT Status;
    ULONG Count;
    IWbemClassObject **Objects;
} WMI_COLLECTOR;

typedef struct _WMI_REGISTRAR
{
    IWbemDecoupledBasicEventProvider IWbemDecoupledRegistrar_iface;
    BOOL EventProvider;
    LONG Refs;
    CRITICAL_SECTION Lock;
    IWbemServices *Provider;
    IWbemContext *Context;
    WCHAR *Namespace;
    WCHAR *Name;
    WCHAR *KeyName;
    WCHAR PipeName[96];
    PSECURITY_DESCRIPTOR Security;
    HANDLE Thread;
    HANDLE Stop;
    HANDLE Ready;
    BOOL Registered;
} WMI_REGISTRAR;

static const IWbemObjectSinkVtbl WmiCollectorVtbl;
static const IWbemProviderInitSinkVtbl WmiInitSinkVtbl;
static const IWbemDecoupledBasicEventProviderVtbl WmiRegistrarVtbl;

static const WCHAR *
WmiStripRoot(const WCHAR *Namespace)
{
    if (!Namespace)
        return L"";
    if ((Namespace[0] == L'\\' && Namespace[1] == L'\\') || (Namespace[0] == L'/' && Namespace[1] == L'/'))
    {
        Namespace += 2;
        while (*Namespace && *Namespace != L'\\' && *Namespace != L'/')
            Namespace++;
        if (*Namespace)
            Namespace++;
    }
    if (!_wcsnicmp(Namespace, L"root", 4) && (Namespace[4] == L'\\' || Namespace[4] == L'/' || !Namespace[4]))
        Namespace += Namespace[4] ? 5 : 4;
    return Namespace;
}

static WCHAR *
WmiRegistrationKey(const WCHAR *Namespace, const WCHAR *Provider)
{
    SIZE_T Length = wcslen(Namespace) + wcslen(Provider) + 3;
    WCHAR *Key = malloc(Length * sizeof(WCHAR)), *Position;

    if (!Key)
        return NULL;
    swprintf(Key, Length, L"%ls::%ls", Namespace, Provider);
    for (Position = Key; *Position; Position++)
        if (*Position == L'\\' || *Position == L'/')
            *Position = L'#';
    return Key;
}

/* Collector */

static inline WMI_COLLECTOR *
WmiCollectorFromSink(IWbemObjectSink *Iface)
{
    return CONTAINING_RECORD(Iface, WMI_COLLECTOR, IWbemObjectSink_iface);
}

static inline WMI_COLLECTOR *
WmiCollectorFromInitSink(IWbemProviderInitSink *Iface)
{
    return CONTAINING_RECORD(Iface, WMI_COLLECTOR, IWbemProviderInitSink_iface);
}

static WMI_COLLECTOR *
WmiCollectorCreate(void)
{
    WMI_COLLECTOR *Collector = calloc(1, sizeof(*Collector));

    if (!Collector)
        return NULL;
    Collector->IWbemObjectSink_iface.lpVtbl = &WmiCollectorVtbl;
    Collector->IWbemProviderInitSink_iface.lpVtbl = &WmiInitSinkVtbl;
    Collector->Refs = 1;
    Collector->Status = WBEM_E_FAILED;
    InitializeCriticalSection(&Collector->Lock);
    if (!(Collector->Done = CreateEventW(NULL, TRUE, FALSE, NULL)))
    {
        DeleteCriticalSection(&Collector->Lock);
        free(Collector);
        return NULL;
    }
    return Collector;
}

static void
WmiCollectorRelease(WMI_COLLECTOR *Collector)
{
    if (InterlockedDecrement(&Collector->Refs))
        return;
    WmiFreeObjects(Collector->Count, Collector->Objects);
    CloseHandle(Collector->Done);
    DeleteCriticalSection(&Collector->Lock);
    free(Collector);
}

static HRESULT
WmiCollectorQueryInterface(WMI_COLLECTOR *Collector, REFIID Iid, void **Result)
{
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IWbemObjectSink))
        *Result = &Collector->IWbemObjectSink_iface;
    else if (IsEqualGUID(Iid, &IID_IWbemProviderInitSink))
        *Result = &Collector->IWbemProviderInitSink_iface;
    else
    {
        *Result = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&Collector->Refs);
    return S_OK;
}

static HRESULT WINAPI
WmiCollector_QueryInterface(IWbemObjectSink *Iface, REFIID Iid, void **Result)
{
    return WmiCollectorQueryInterface(WmiCollectorFromSink(Iface), Iid, Result);
}

static ULONG WINAPI
WmiCollector_AddRef(IWbemObjectSink *Iface)
{
    return InterlockedIncrement(&WmiCollectorFromSink(Iface)->Refs);
}

static ULONG WINAPI
WmiCollector_Release(IWbemObjectSink *Iface)
{
    WMI_COLLECTOR *Collector = WmiCollectorFromSink(Iface);
    LONG Refs = Collector->Refs - 1;

    WmiCollectorRelease(Collector);
    return Refs;
}

static HRESULT WINAPI
WmiCollector_Indicate(IWbemObjectSink *Iface, LONG Count, IWbemClassObject **Objects)
{
    WMI_COLLECTOR *Collector = WmiCollectorFromSink(Iface);
    LONG Index;
    HRESULT hr = WBEM_S_NO_ERROR;

    if (Count < 0 || (Count && !Objects))
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&Collector->Lock);
    for (Index = 0; Index < Count && SUCCEEDED(hr); Index++)
    {
        IWbemClassObject **Grown, *Copy;

        if (!Objects[Index])
            continue;
        hr = IWbemClassObject_Clone(Objects[Index], &Copy);
        if (FAILED(hr))
            break;
        if (!(Grown = realloc(Collector->Objects, (Collector->Count + 1) * sizeof(*Grown))))
        {
            IWbemClassObject_Release(Copy);
            hr = WBEM_E_OUT_OF_MEMORY;
            break;
        }
        Collector->Objects = Grown;
        Grown[Collector->Count++] = Copy;
    }
    LeaveCriticalSection(&Collector->Lock);
    return hr;
}

static HRESULT WINAPI
WmiCollector_SetStatus(IWbemObjectSink *Iface, LONG Flags, HRESULT Result, BSTR Parameter, IWbemClassObject *Object)
{
    WMI_COLLECTOR *Collector = WmiCollectorFromSink(Iface);

    if (Flags != WBEM_STATUS_COMPLETE)
        return WBEM_S_NO_ERROR;
    EnterCriticalSection(&Collector->Lock);
    Collector->Status = Result;
    LeaveCriticalSection(&Collector->Lock);
    SetEvent(Collector->Done);
    return WBEM_S_NO_ERROR;
}

static const IWbemObjectSinkVtbl WmiCollectorVtbl =
{
    WmiCollector_QueryInterface, WmiCollector_AddRef, WmiCollector_Release, WmiCollector_Indicate,
    WmiCollector_SetStatus
};

static HRESULT WINAPI
WmiInitSink_QueryInterface(IWbemProviderInitSink *Iface, REFIID Iid, void **Result)
{
    return WmiCollectorQueryInterface(WmiCollectorFromInitSink(Iface), Iid, Result);
}

static ULONG WINAPI
WmiInitSink_AddRef(IWbemProviderInitSink *Iface)
{
    return InterlockedIncrement(&WmiCollectorFromInitSink(Iface)->Refs);
}

static ULONG WINAPI
WmiInitSink_Release(IWbemProviderInitSink *Iface)
{
    WMI_COLLECTOR *Collector = WmiCollectorFromInitSink(Iface);
    LONG Refs = Collector->Refs - 1;

    WmiCollectorRelease(Collector);
    return Refs;
}

static HRESULT WINAPI
WmiInitSink_SetStatus(IWbemProviderInitSink *Iface, LONG Status, LONG Flags)
{
    WMI_COLLECTOR *Collector = WmiCollectorFromInitSink(Iface);

    EnterCriticalSection(&Collector->Lock);
    Collector->Status = Status == WBEM_S_INITIALIZED ? S_OK : (FAILED(Status) ? Status : WBEM_E_PROVIDER_FAILURE);
    LeaveCriticalSection(&Collector->Lock);
    SetEvent(Collector->Done);
    return WBEM_S_NO_ERROR;
}

static const IWbemProviderInitSinkVtbl WmiInitSinkVtbl =
{
    WmiInitSink_QueryInterface, WmiInitSink_AddRef, WmiInitSink_Release, WmiInitSink_SetStatus
};

static HRESULT
WmiCollectorWait(WMI_COLLECTOR *Collector, HRESULT CallResult)
{
    HRESULT hr;

    if (FAILED(CallResult))
        return CallResult;
    if (WaitForSingleObject(Collector->Done, WMI_PROVIDER_TIMEOUT) != WAIT_OBJECT_0)
        return WBEM_E_TIMED_OUT;
    EnterCriticalSection(&Collector->Lock);
    hr = Collector->Status;
    LeaveCriticalSection(&Collector->Lock);
    return hr;
}

/* Messages: a ULONG length, then the body. The server's handle is overlapped and its transfers end with Stop. */

typedef struct _WMI_PIPE
{
    HANDLE Handle;
    OVERLAPPED *Overlapped;
    HANDLE Stop;
} WMI_PIPE;

static BOOL
WmiPipeTransfer(const WMI_PIPE *Pipe, BOOL Write, void *Data, DWORD Size, DWORD *Done)
{
    HANDLE Waits[2];
    BOOL Ok;

    if (!Pipe->Overlapped)
        return Write ? WriteFile(Pipe->Handle, Data, Size, Done, NULL) : ReadFile(Pipe->Handle, Data, Size, Done, NULL);

    ResetEvent(Pipe->Overlapped->hEvent);
    Ok = Write ? WriteFile(Pipe->Handle, Data, Size, Done, Pipe->Overlapped) :
                 ReadFile(Pipe->Handle, Data, Size, Done, Pipe->Overlapped);
    if (Ok || GetLastError() != ERROR_IO_PENDING)
        return Ok;
    Waits[0] = Pipe->Overlapped->hEvent;
    Waits[1] = Pipe->Stop;
    if (WaitForMultipleObjects(2, Waits, FALSE, INFINITE) != WAIT_OBJECT_0)
    {
        CancelIo(Pipe->Handle);
        GetOverlappedResult(Pipe->Handle, Pipe->Overlapped, Done, TRUE);
        return FALSE;
    }
    return GetOverlappedResult(Pipe->Handle, Pipe->Overlapped, Done, FALSE);
}

static BOOL
WmiPipeWrite(const WMI_PIPE *Pipe, const WMI_BUFFER *Buffer)
{
    ULONG Length = (ULONG)Buffer->Size;
    DWORD Done;
    SIZE_T Offset = 0;

    if (Buffer->Failed || Buffer->Size > WMI_MAX_MESSAGE)
        return FALSE;
    if (!WmiPipeTransfer(Pipe, TRUE, &Length, sizeof(Length), &Done) || Done != sizeof(Length))
        return FALSE;
    while (Offset < Buffer->Size)
    {
        DWORD Chunk = (DWORD)min(Buffer->Size - Offset, WMI_PIPE_BUFFER);

        if (!WmiPipeTransfer(Pipe, TRUE, Buffer->Data + Offset, Chunk, &Done) || !Done)
            return FALSE;
        Offset += Done;
    }
    return TRUE;
}

static BOOL
WmiPipeRead(const WMI_PIPE *Pipe, BYTE **Data, ULONG *Size)
{
    ULONG Length, Offset = 0;
    DWORD Done;

    *Data = NULL;
    if (!WmiPipeTransfer(Pipe, FALSE, &Length, sizeof(Length), &Done) || Done != sizeof(Length) ||
        Length > WMI_MAX_MESSAGE)
        return FALSE;
    if (!(*Data = malloc(max(Length, 1))))
        return FALSE;
    while (Offset < Length)
    {
        if (!WmiPipeTransfer(Pipe, FALSE, *Data + Offset, Length - Offset, &Done) || !Done)
        {
            free(*Data);
            *Data = NULL;
            return FALSE;
        }
        Offset += Done;
    }
    *Size = Length;
    return TRUE;
}

/* Provider process: serve one request */

static HRESULT
WmiServeRequest(WMI_REGISTRAR *Registrar, WMI_READER *Request, WMI_BUFFER *Response)
{
    ULONG Operation = WmiReaderGetUlong(Request), Index;
    WCHAR *Class = WmiReaderGetString(Request), *Path = WmiReaderGetString(Request), *Method = WmiReaderGetString(Request);
    IWbemClassObject *InParams = NULL;
    WMI_COLLECTOR *Collector = NULL;
    BSTR ClassString = NULL, PathString = NULL, MethodString = NULL;
    HRESULT hr = S_OK;

    if (WmiReaderGetUlong(Request))
        hr = WmiObjectDeserialize(Request, &InParams);
    if (SUCCEEDED(hr) && Request->Failed)
        hr = WBEM_E_INVALID_PARAMETER;
    if (SUCCEEDED(hr) && !(Collector = WmiCollectorCreate()))
        hr = WBEM_E_OUT_OF_MEMORY;
    if (SUCCEEDED(hr))
    {
        ClassString = Class ? SysAllocString(Class) : NULL;
        PathString = Path ? SysAllocString(Path) : NULL;
        MethodString = Method ? SysAllocString(Method) : NULL;
        switch (Operation)
        {
        case WmiOperationEnumerate:
            hr = IWbemServices_CreateInstanceEnumAsync(Registrar->Provider, ClassString, 0, Registrar->Context,
                                                       &Collector->IWbemObjectSink_iface);
            break;
        case WmiOperationGetObject:
            hr = IWbemServices_GetObjectAsync(Registrar->Provider, PathString, 0, Registrar->Context,
                                              &Collector->IWbemObjectSink_iface);
            break;
        case WmiOperationExecMethod:
            hr = IWbemServices_ExecMethodAsync(Registrar->Provider, PathString, MethodString, 0, Registrar->Context,
                                               InParams, &Collector->IWbemObjectSink_iface);
            break;
        default:
            hr = WBEM_E_INVALID_OPERATION;
            break;
        }
        hr = WmiCollectorWait(Collector, hr);
    }
    WmiBufferPutUlong(Response, (ULONG)hr);
    WmiBufferPutUlong(Response, Collector && SUCCEEDED(hr) ? Collector->Count : 0);
    for (Index = 0; Collector && SUCCEEDED(hr) && Index < Collector->Count; Index++)
        if (FAILED(WmiObjectSerialize(Collector->Objects[Index], Response)))
            Response->Failed = TRUE;
    if (Collector)
        WmiCollectorRelease(Collector);
    if (InParams)
        IWbemClassObject_Release(InParams);
    SysFreeString(ClassString);
    SysFreeString(PathString);
    SysFreeString(MethodString);
    free(Class);
    free(Path);
    free(Method);
    return Response->Failed ? WBEM_E_OUT_OF_MEMORY : S_OK;
}

static HANDLE
WmiCreatePipeInstance(WMI_REGISTRAR *Registrar, SECURITY_ATTRIBUTES *Attributes)
{
    return CreateNamedPipeW(Registrar->PipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, WMI_PIPE_BUFFER,
                            WMI_PIPE_BUFFER, 0, Attributes);
}

static DWORD WINAPI
WmiServerThread(void *Context)
{
    WMI_REGISTRAR *Registrar = Context;
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), Registrar->Security, FALSE };
    OVERLAPPED Overlapped = { 0 };
    HANDLE Pipe = INVALID_HANDLE_VALUE;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Overlapped.hEvent)
        Pipe = WmiCreatePipeInstance(Registrar, &Attributes);
    SetEvent(Registrar->Ready);
    while (Pipe != INVALID_HANDLE_VALUE)
    {
        HANDLE Waits[2] = { Overlapped.hEvent, Registrar->Stop };
        HANDLE Next;
        BOOL Connected;
        BYTE *Data;
        ULONG Size;

        ResetEvent(Overlapped.hEvent);
        Connected = ConnectNamedPipe(Pipe, &Overlapped) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!Connected && GetLastError() == ERROR_IO_PENDING)
        {
            DWORD Done;

            if (WaitForMultipleObjects(2, Waits, FALSE, INFINITE) != WAIT_OBJECT_0)
            {
                CancelIo(Pipe);
                break;
            }
            Connected = GetOverlappedResult(Pipe, &Overlapped, &Done, FALSE);
        }
        /* The next client finds a listening instance while this one is served */
        Next = WmiCreatePipeInstance(Registrar, &Attributes);
        if (Connected)
        {
            WMI_PIPE Io = { Pipe, &Overlapped, Registrar->Stop };

            /* TODO: serve several clients at once */
            if (WmiPipeRead(&Io, &Data, &Size))
            {
                WMI_READER Request = { Data, Size, 0, FALSE };
                WMI_BUFFER Response = { 0 };

                EnterCriticalSection(&Registrar->Lock);
                WmiServeRequest(Registrar, &Request, &Response);
                LeaveCriticalSection(&Registrar->Lock);
                if (WmiPipeWrite(&Io, &Response))
                {
                    BYTE End;
                    DWORD Done;

                    /* Disconnecting discards what the client has not read: wait until it closes its end */
                    WmiPipeTransfer(&Io, FALSE, &End, sizeof(End), &Done);
                }
                WmiBufferFree(&Response);
                free(Data);
            }
            DisconnectNamedPipe(Pipe);
        }
        CloseHandle(Pipe);
        Pipe = Next;
        if (WaitForSingleObject(Registrar->Stop, 0) == WAIT_OBJECT_0)
            break;
    }
    if (Pipe != INVALID_HANDLE_VALUE)
        CloseHandle(Pipe);
    if (Overlapped.hEvent)
        CloseHandle(Overlapped.hEvent);
    CoUninitialize();
    return 0;
}

/* IWbemDecoupledRegistrar */

static inline WMI_REGISTRAR *
WmiRegistrarFromIface(IWbemDecoupledBasicEventProvider *Iface)
{
    return CONTAINING_RECORD(Iface, WMI_REGISTRAR, IWbemDecoupledRegistrar_iface);
}

static HRESULT WINAPI
WmiRegistrar_QueryInterface(IWbemDecoupledBasicEventProvider *Iface, REFIID Iid, void **Result)
{
    WMI_REGISTRAR *Registrar = WmiRegistrarFromIface(Iface);

    if (!Result)
        return E_POINTER;
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IWbemDecoupledRegistrar) ||
        (Registrar->EventProvider && IsEqualGUID(Iid, &IID_IWbemDecoupledBasicEventProvider)))
    {
        *Result = Iface;
        IWbemDecoupledBasicEventProvider_AddRef(Iface);
        return S_OK;
    }
    *Result = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
WmiRegistrar_AddRef(IWbemDecoupledBasicEventProvider *Iface)
{
    return InterlockedIncrement(&WmiRegistrarFromIface(Iface)->Refs);
}

static void
WmiRegistrarStop(WMI_REGISTRAR *Registrar)
{
    HKEY Key;

    if (Registrar->KeyName && !RegOpenKeyExW(HKEY_LOCAL_MACHINE, WMI_DECOUPLED_KEY, 0, KEY_WRITE, &Key))
    {
        RegDeleteKeyW(Key, Registrar->KeyName);
        RegCloseKey(Key);
    }
    if (Registrar->Thread)
    {
        HANDLE Wake;

        SetEvent(Registrar->Stop);
        Wake = CreateFileW(Registrar->PipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (Wake != INVALID_HANDLE_VALUE)
            CloseHandle(Wake);
        WaitForSingleObject(Registrar->Thread, INFINITE);
        CloseHandle(Registrar->Thread);
        Registrar->Thread = NULL;
    }
    if (Registrar->Provider)
        IWbemServices_Release(Registrar->Provider);
    if (Registrar->Context)
        IWbemContext_Release(Registrar->Context);
    if (Registrar->Security)
        LocalFree(Registrar->Security);
    free(Registrar->Namespace);
    free(Registrar->Name);
    free(Registrar->KeyName);
    Registrar->Provider = NULL;
    Registrar->Context = NULL;
    Registrar->Security = NULL;
    Registrar->Namespace = NULL;
    Registrar->Name = NULL;
    Registrar->KeyName = NULL;
    Registrar->Registered = FALSE;
}

static ULONG WINAPI
WmiRegistrar_Release(IWbemDecoupledBasicEventProvider *Iface)
{
    WMI_REGISTRAR *Registrar = WmiRegistrarFromIface(Iface);
    LONG Refs = InterlockedDecrement(&Registrar->Refs);

    if (!Refs)
    {
        WmiRegistrarStop(Registrar);
        CloseHandle(Registrar->Stop);
        CloseHandle(Registrar->Ready);
        DeleteCriticalSection(&Registrar->Lock);
        free(Registrar);
    }
    return Refs;
}

static HRESULT
WmiFindProviderRegistration(const WCHAR *Namespace, const WCHAR *Name, IWbemClassObject **Provider)
{
    WMI_PATH Path = { 0 };
    WCHAR *Text;
    SIZE_T Length = wcslen(Name) * 2 + 32;
    const WCHAR *Source;
    WCHAR *Out;
    HRESULT hr;

    if (!(Text = malloc(Length * sizeof(WCHAR))))
        return WBEM_E_OUT_OF_MEMORY;
    Out = Text + swprintf(Text, Length, L"__Win32Provider.Name=\"");
    for (Source = Name; *Source; Source++)
    {
        if (*Source == L'"' || *Source == L'\\')
            *Out++ = L'\\';
        *Out++ = *Source;
    }
    wcscpy(Out, L"\"");
    hr = WmiParsePath(Text, &Path);
    free(Text);
    if (SUCCEEDED(hr))
        hr = WmiRepoGetInstance(Namespace, &Path, Provider);
    WmiFreePath(&Path);
    return hr;
}

static HRESULT WINAPI
WmiRegistrar_Register(IWbemDecoupledBasicEventProvider *Iface, LONG Flags, IWbemContext *Context, LPCWSTR User,
                      LPCWSTR Locale, LPCWSTR Scope, LPCWSTR Registration, IUnknown *Unknown)
{
    WMI_REGISTRAR *Registrar = WmiRegistrarFromIface(Iface);
    IWbemProviderInit *Init = NULL;
    IWbemServices *Services = NULL;
    IWbemClassObject *Provider = NULL;
    WMI_COLLECTOR *Collector = NULL;
    VARIANT Value;
    HKEY Key = NULL, Entry = NULL;
    DWORD ProcessId = GetCurrentProcessId();
    HRESULT hr = S_OK;

    if (!Scope || !Registration || (!Unknown && !Registrar->EventProvider))
        return WBEM_E_INVALID_PARAMETER;
    EnterCriticalSection(&Registrar->Lock);
    if (Registrar->Registered)
        hr = WBEM_E_ALREADY_EXISTS;
    if (SUCCEEDED(hr) && (!(Registrar->Namespace = _wcsdup(WmiStripRoot(Scope))) ||
                          !(Registrar->Name = _wcsdup(Registration)) ||
                          !(Registrar->KeyName = WmiRegistrationKey(Registrar->Namespace, Registration))))
        hr = WBEM_E_OUT_OF_MEMORY;
    if (SUCCEEDED(hr))
        hr = WmiFindProviderRegistration(Registrar->Namespace, Registration, &Provider);
    if (hr == WBEM_E_NOT_FOUND || hr == WBEM_E_INVALID_CLASS)
        hr = WBEM_E_PROVIDER_NOT_FOUND;
    if (SUCCEEDED(hr))
    {
        VariantInit(&Value);
        if (SUCCEEDED(IWbemClassObject_Get(Provider, L"SecurityDescriptor", 0, &Value, NULL, NULL)) &&
            V_VT(&Value) == VT_BSTR &&
            !ConvertStringSecurityDescriptorToSecurityDescriptorW(V_BSTR(&Value), SDDL_REVISION_1,
                                                                  &Registrar->Security, NULL))
            hr = WBEM_E_INVALID_PARAMETER;
        VariantClear(&Value);
    }
    if (SUCCEEDED(hr) && Unknown &&
        FAILED(IUnknown_QueryInterface(Unknown, &IID_IWbemServices, (void **)&Registrar->Provider)) &&
        !Registrar->EventProvider)
        /* TODO: decoupled event providers registered through an IWbemEventProvider object */
        hr = WBEM_E_INVALID_PARAMETER;
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(&CLSID_WbemContext, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemContext,
                              (void **)&Registrar->Context);
    if (SUCCEEDED(hr) && Unknown && SUCCEEDED(IUnknown_QueryInterface(Unknown, &IID_IWbemProviderInit, (void **)&Init)))
    {
        hr = WmiRepoServicesCreate(Registrar->Namespace, &Services);
        if (SUCCEEDED(hr) && !(Collector = WmiCollectorCreate()))
            hr = WBEM_E_OUT_OF_MEMORY;
        if (SUCCEEDED(hr))
        {
            hr = IWbemProviderInit_Initialize(Init, (LPWSTR)User, 0, (LPWSTR)Scope, (LPWSTR)Locale, Services,
                                              Registrar->Context, &Collector->IWbemProviderInitSink_iface);
            hr = WmiCollectorWait(Collector, hr);
        }
    }
    if (SUCCEEDED(hr) && Registrar->Provider)
    {
        swprintf(Registrar->PipeName, ARRAYSIZE(Registrar->PipeName), L"\\\\.\\pipe\\LiberNT.Wmi.%lu.%p", ProcessId,
                 Registrar);
        ResetEvent(Registrar->Stop);
        ResetEvent(Registrar->Ready);
        if (!(Registrar->Thread = CreateThread(NULL, 0, WmiServerThread, Registrar, 0, NULL)))
            hr = WBEM_E_OUT_OF_MEMORY;
        else
            WaitForSingleObject(Registrar->Ready, INFINITE);
    }
    if (SUCCEEDED(hr) && Registrar->Provider)
    {
        DWORD Disposition;

        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, WMI_DECOUPLED_KEY, 0, NULL, REG_OPTION_VOLATILE, KEY_WRITE, NULL, &Key,
                            &Disposition) ||
            RegCreateKeyExW(Key, Registrar->KeyName, 0, NULL, REG_OPTION_VOLATILE, KEY_WRITE, NULL, &Entry,
                            &Disposition) ||
            RegSetValueExW(Entry, L"Pipe", 0, REG_SZ, (const BYTE *)Registrar->PipeName,
                           (DWORD)((wcslen(Registrar->PipeName) + 1) * sizeof(WCHAR))) ||
            RegSetValueExW(Entry, L"ProcessId", 0, REG_DWORD, (const BYTE *)&ProcessId, sizeof(ProcessId)))
            hr = WBEM_E_ACCESS_DENIED;
    }
    if (Entry)
        RegCloseKey(Entry);
    if (Key)
        RegCloseKey(Key);
    if (SUCCEEDED(hr))
        Registrar->Registered = TRUE;
    else
        WmiRegistrarStop(Registrar);
    LeaveCriticalSection(&Registrar->Lock);
    if (Collector)
        WmiCollectorRelease(Collector);
    if (Services)
        IWbemServices_Release(Services);
    if (Init)
        IWbemProviderInit_Release(Init);
    if (Provider)
        IWbemClassObject_Release(Provider);
    return hr;
}

static HRESULT WINAPI
WmiRegistrar_UnRegister(IWbemDecoupledBasicEventProvider *Iface)
{
    WMI_REGISTRAR *Registrar = WmiRegistrarFromIface(Iface);
    HRESULT hr = S_OK;

    EnterCriticalSection(&Registrar->Lock);
    if (!Registrar->Registered)
        hr = WBEM_E_FAILED;
    LeaveCriticalSection(&Registrar->Lock);
    if (SUCCEEDED(hr))
        WmiRegistrarStop(Registrar);
    return hr;
}

/* Events a decoupled provider reports through the sink of IWbemDecoupledBasicEventProvider */
typedef struct _WMI_EVENT_SINK
{
    IWbemObjectSink IWbemObjectSink_iface;
    LONG Refs;
} WMI_EVENT_SINK;

static HRESULT WINAPI
WmiEventSink_QueryInterface(IWbemObjectSink *Iface, REFIID Iid, void **Result)
{
    if (!Result)
        return E_POINTER;
    if (IsEqualGUID(Iid, &IID_IUnknown) || IsEqualGUID(Iid, &IID_IWbemObjectSink))
    {
        *Result = Iface;
        IWbemObjectSink_AddRef(Iface);
        return S_OK;
    }
    *Result = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
WmiEventSink_AddRef(IWbemObjectSink *Iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(Iface, WMI_EVENT_SINK, IWbemObjectSink_iface)->Refs);
}

static ULONG WINAPI
WmiEventSink_Release(IWbemObjectSink *Iface)
{
    WMI_EVENT_SINK *Sink = CONTAINING_RECORD(Iface, WMI_EVENT_SINK, IWbemObjectSink_iface);
    LONG Refs = InterlockedDecrement(&Sink->Refs);

    if (!Refs)
        free(Sink);
    return Refs;
}

static HRESULT WINAPI
WmiEventSink_Indicate(IWbemObjectSink *Iface, LONG Count, IWbemClassObject **Objects)
{
    /* FIXME: events are accepted and dropped; nothing delivers them to event consumers yet */
    return (Count < 0 || (Count && !Objects)) ? WBEM_E_INVALID_PARAMETER : WBEM_S_NO_ERROR;
}

static HRESULT WINAPI
WmiEventSink_SetStatus(IWbemObjectSink *Iface, LONG Flags, HRESULT Result, BSTR Parameter, IWbemClassObject *Object)
{
    return WBEM_S_NO_ERROR;
}

static const IWbemObjectSinkVtbl WmiEventSinkVtbl =
{
    WmiEventSink_QueryInterface, WmiEventSink_AddRef, WmiEventSink_Release, WmiEventSink_Indicate,
    WmiEventSink_SetStatus
};

static HRESULT WINAPI
WmiRegistrar_GetSink(IWbemDecoupledBasicEventProvider *Iface, LONG Flags, IWbemContext *Context,
                     IWbemObjectSink **Sink)
{
    WMI_REGISTRAR *Registrar = WmiRegistrarFromIface(Iface);
    WMI_EVENT_SINK *Object;

    if (!Sink)
        return WBEM_E_INVALID_PARAMETER;
    *Sink = NULL;
    if (!Registrar->Registered)
        return WBEM_E_FAILED;
    if (!(Object = calloc(1, sizeof(*Object))))
        return WBEM_E_OUT_OF_MEMORY;
    Object->IWbemObjectSink_iface.lpVtbl = &WmiEventSinkVtbl;
    Object->Refs = 1;
    *Sink = &Object->IWbemObjectSink_iface;
    return WBEM_S_NO_ERROR;
}

static HRESULT WINAPI
WmiRegistrar_GetService(IWbemDecoupledBasicEventProvider *Iface, LONG Flags, IWbemContext *Context,
                        IWbemServices **Service)
{
    WMI_REGISTRAR *Registrar = WmiRegistrarFromIface(Iface);

    if (!Service)
        return WBEM_E_INVALID_PARAMETER;
    *Service = NULL;
    if (!Registrar->Registered)
        return WBEM_E_FAILED;
    return WmiRepoServicesCreate(Registrar->Namespace, Service);
}

static const IWbemDecoupledBasicEventProviderVtbl WmiRegistrarVtbl =
{
    WmiRegistrar_QueryInterface, WmiRegistrar_AddRef, WmiRegistrar_Release, WmiRegistrar_Register,
    WmiRegistrar_UnRegister, WmiRegistrar_GetSink, WmiRegistrar_GetService
};

static HRESULT
WmiRegistrarCreate(void **Object, REFIID Iid, BOOL EventProvider)
{
    WMI_REGISTRAR *Registrar = calloc(1, sizeof(*Registrar));
    HRESULT hr;

    if (!Registrar)
        return E_OUTOFMEMORY;
    Registrar->IWbemDecoupledRegistrar_iface.lpVtbl = &WmiRegistrarVtbl;
    Registrar->EventProvider = EventProvider;
    Registrar->Refs = 1;
    InitializeCriticalSection(&Registrar->Lock);
    Registrar->Stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    Registrar->Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Registrar->Stop || !Registrar->Ready)
    {
        if (Registrar->Stop)
            CloseHandle(Registrar->Stop);
        if (Registrar->Ready)
            CloseHandle(Registrar->Ready);
        DeleteCriticalSection(&Registrar->Lock);
        free(Registrar);
        return E_OUTOFMEMORY;
    }
    hr = WmiRegistrar_QueryInterface(&Registrar->IWbemDecoupledRegistrar_iface, Iid, Object);
    WmiRegistrar_Release(&Registrar->IWbemDecoupledRegistrar_iface);
    return hr;
}

HRESULT
WmiDecoupledRegistrarCreate(void **Object, REFIID Iid)
{
    return WmiRegistrarCreate(Object, Iid, FALSE);
}

HRESULT
WmiDecoupledEventProviderCreate(void **Object, REFIID Iid)
{
    return WmiRegistrarCreate(Object, Iid, TRUE);
}

/* Client process: send a request to the provider registered for a class */

BOOL
WmiClassIsDynamic(IWbemClassObject *Class)
{
    VARIANT Value;
    BOOL Dynamic = FALSE;

    if (!WmiObjectIsOurs(Class))
        return FALSE;
    if (SUCCEEDED(WmiObjectGetClassQualifier(Class, L"dynamic", &Value)))
        Dynamic = V_VT(&Value) == VT_BOOL && V_BOOL(&Value);
    VariantClear(&Value);
    return Dynamic;
}

static HRESULT
WmiFindRegistration(const WCHAR *Namespace, IWbemClassObject *Class, WCHAR *Pipe, DWORD PipeSize)
{
    VARIANT Provider;
    WCHAR *KeyName;
    HKEY Key;
    DWORD Type, Size = PipeSize;
    LSTATUS Status;

    VariantInit(&Provider);
    if (FAILED(WmiObjectGetClassQualifier(Class, L"provider", &Provider)) || V_VT(&Provider) != VT_BSTR)
    {
        VariantClear(&Provider);
        return WBEM_E_PROVIDER_NOT_FOUND;
    }
    /* TODO: in-process and out-of-process COM providers named by __Win32Provider.CLSID */
    KeyName = WmiRegistrationKey(Namespace, V_BSTR(&Provider));
    VariantClear(&Provider);
    if (!KeyName)
        return WBEM_E_OUT_OF_MEMORY;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, WMI_DECOUPLED_KEY, 0, KEY_READ, &Key))
    {
        free(KeyName);
        return WBEM_E_PROVIDER_NOT_FOUND;
    }
    Status = RegGetValueW(Key, KeyName, L"Pipe", RRF_RT_REG_SZ, &Type, Pipe, &Size);
    RegCloseKey(Key);
    free(KeyName);
    return Status ? WBEM_E_PROVIDER_NOT_FOUND : S_OK;
}

static HRESULT
WmiProviderRequest(const WCHAR *Namespace, IWbemClassObject *Class, WMI_PROVIDER_OPERATION Operation,
                   const WCHAR *Path, const WCHAR *Method, IWbemClassObject *InParams, ULONG *Count,
                   IWbemClassObject ***Objects)
{
    WCHAR PipeName[128];
    WMI_BUFFER Request = { 0 };
    WMI_READER Reader = { 0 };
    HANDLE Pipe = INVALID_HANDLE_VALUE;
    WMI_PIPE Io = { 0 };
    BYTE *Data = NULL;
    ULONG Size = 0, Total, Index;
    WCHAR *Full;
    HRESULT hr;

    *Count = 0;
    *Objects = NULL;
    hr = WmiFindRegistration(Namespace, Class, PipeName, sizeof(PipeName));
    if (FAILED(hr))
        return hr;
    for (Index = 0; Index < 3; Index++)
    {
        Pipe = CreateFileW(PipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (Pipe != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(PipeName, 5000))
            break;
    }
    if (Pipe == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_ACCESS_DENIED ? WBEM_E_ACCESS_DENIED : WBEM_E_PROVIDER_NOT_FOUND;
    WmiBufferPutUlong(&Request, Operation);
    WmiBufferPutString(&Request, WmiObjectClassName(Class));
    WmiBufferPutString(&Request, Path);
    WmiBufferPutString(&Request, Method);
    WmiBufferPutUlong(&Request, InParams != NULL);
    if (InParams && FAILED(WmiObjectSerialize(InParams, &Request)))
        Request.Failed = TRUE;
    Io.Handle = Pipe;
    if (!WmiPipeWrite(&Io, &Request) || !WmiPipeRead(&Io, &Data, &Size))
        hr = WBEM_E_TRANSPORT_FAILURE;
    CloseHandle(Pipe);
    WmiBufferFree(&Request);
    if (FAILED(hr))
        return hr;
    Reader.Data = Data;
    Reader.Size = Size;
    hr = (HRESULT)WmiReaderGetUlong(&Reader);
    Total = WmiReaderGetUlong(&Reader);
    if (Reader.Failed || Total > Size)
        hr = WBEM_E_TRANSPORT_FAILURE;
    Full = WmiFullNamespace(Namespace);
    if (SUCCEEDED(hr) && Total && !(*Objects = calloc(Total, sizeof(**Objects))))
        hr = WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; SUCCEEDED(hr) && Index < Total; Index++)
    {
        WCHAR Computer[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD Length = ARRAYSIZE(Computer);

        hr = WmiObjectDeserialize(&Reader, &(*Objects)[Index]);
        if (FAILED(hr))
            break;
        (*Count)++;
        if (!GetComputerNameW(Computer, &Length))
            wcscpy(Computer, L".");
        WmiObjectSetLocation((*Objects)[Index], Computer, Full);
    }
    free(Full);
    free(Data);
    if (FAILED(hr))
    {
        WmiFreeObjects(*Count, *Objects);
        *Count = 0;
        *Objects = NULL;
    }
    return hr;
}

HRESULT
WmiProviderEnumerate(const WCHAR *Namespace, IWbemClassObject *Class, ULONG *Count, IWbemClassObject ***Instances)
{
    HRESULT hr = WmiProviderRequest(Namespace, Class, WmiOperationEnumerate, NULL, NULL, NULL, Count, Instances);

    /* With no provider to ask, a dynamic class has no instances */
    return hr == WBEM_E_PROVIDER_NOT_FOUND ? S_OK : hr;
}

HRESULT
WmiProviderGetObject(const WCHAR *Namespace, IWbemClassObject *Class, const WCHAR *Path, IWbemClassObject **Instance)
{
    IWbemClassObject **Objects;
    ULONG Count;
    HRESULT hr = WmiProviderRequest(Namespace, Class, WmiOperationGetObject, Path, NULL, NULL, &Count, &Objects);

    *Instance = NULL;
    if (hr == WBEM_E_PROVIDER_NOT_FOUND)
        return WBEM_E_NOT_FOUND;
    if (FAILED(hr))
        return hr;
    if (!Count)
        return WBEM_E_NOT_FOUND;
    *Instance = Objects[0];
    Objects[0] = NULL;
    WmiFreeObjects(Count, Objects);
    return S_OK;
}

HRESULT
WmiProviderExecMethod(const WCHAR *Namespace, IWbemClassObject *Class, const WCHAR *Path, const WCHAR *Method,
                      IWbemClassObject *InParams, IWbemClassObject **OutParams)
{
    IWbemClassObject **Objects;
    ULONG Count;
    HRESULT hr = WmiProviderRequest(Namespace, Class, WmiOperationExecMethod, Path, Method, InParams, &Count,
                                    &Objects);

    *OutParams = NULL;
    if (FAILED(hr))
        return hr;
    if (Count)
    {
        *OutParams = Objects[0];
        Objects[0] = NULL;
    }
    WmiFreeObjects(Count, Objects);
    return hr;
}
