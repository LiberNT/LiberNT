/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WMI decoupled providers registered through IWbemDecoupledRegistrar
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wmitest.h"

#define PROV_NAMESPACE L"root\\cimv2\\LntWmiProv"
#define PROV_NAME L"Lnt|Decoupled|1.0"
#define READY_EVENT "LntWmiProviderReady"
#define STOP_EVENT "LntWmiProviderStop"

static const char ProvMof[] =
    "#pragma namespace(\"\\\\\\\\.\\\\root\\\\cimv2\\\\LntWmiProv\")\n"
    "instance of __Win32Provider as $P\n"
    "{\n"
    "    ClientLoadableCLSID = NULL;\n"
    "    CLSID = \"{5C1D7B7E-62A1-4F0B-9B4B-0D6B8F7A2C11}\";\n"
    "    HostingModel = \"Decoupled:Com\";\n"
    "    Name = \"Lnt|Decoupled|1.0\";\n"
    "    SecurityDescriptor = \"O:BAG:SYD:(A;;0x10000001;;;BA)(A;;0x10000001;;;SY)\";\n"
    "};\n"
    "instance of __InstanceProviderRegistration\n"
    "{\n"
    "    Provider = $P;\n"
    "    SupportsGet = TRUE;\n"
    "    SupportsPut = FALSE;\n"
    "    SupportsDelete = FALSE;\n"
    "    SupportsEnumeration = TRUE;\n"
    "};\n"
    "instance of __MethodProviderRegistration\n"
    "{\n"
    "    Provider = $P;\n"
    "};\n"
    "[Dynamic, Provider(\"Lnt|Decoupled|1.0\")]\n"
    "class Lnt_Dynamic\n"
    "{\n"
    "    [key] uint32 Id;\n"
    "    string Name;\n"
    "    [Implemented] uint32 Twice([in] uint32 X, [out] uint32 Y);\n"
    "};\n"
    "[Dynamic, Provider(\"Lnt|Decoupled|1.0\")]\n"
    "class Lnt_Empty\n"
    "{\n"
    "    [key] uint32 Id;\n"
    "};\n";

typedef struct _PROVIDER
{
    IWbemProviderInit IWbemProviderInit_iface;
    IWbemServices IWbemServices_iface;
    LONG Refs;
    IWbemServices *Namespace;
    CRITICAL_SECTION Lock;
    FILE *Log;
} PROVIDER;

static PROVIDER Provider;

static void
ProviderLog(const char *Format, ...)
{
    va_list Args;

    EnterCriticalSection(&Provider.Lock);
    va_start(Args, Format);
    vfprintf(Provider.Log, Format, Args);
    va_end(Args);
    fputc('\n', Provider.Log);
    fflush(Provider.Log);
    LeaveCriticalSection(&Provider.Lock);
}

static HRESULT
ProviderQueryInterface(REFIID Iid, void **Object)
{
    if (IsEqualIID(Iid, &IID_IUnknown) || IsEqualIID(Iid, &IID_IWbemProviderInit))
        *Object = &Provider.IWbemProviderInit_iface;
    else if (IsEqualIID(Iid, &IID_IWbemServices))
        *Object = &Provider.IWbemServices_iface;
    else
    {
        *Object = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&Provider.Refs);
    return S_OK;
}

static HRESULT WINAPI Init_QueryInterface(IWbemProviderInit *This, REFIID Iid, void **Object) { return ProviderQueryInterface(Iid, Object); }
static ULONG WINAPI Init_AddRef(IWbemProviderInit *This) { return InterlockedIncrement(&Provider.Refs); }
static ULONG WINAPI Init_Release(IWbemProviderInit *This) { return InterlockedDecrement(&Provider.Refs); }

static HRESULT WINAPI
Init_Initialize(IWbemProviderInit *This, LPWSTR User, LONG Flags, LPWSTR Namespace, LPWSTR Locale,
                IWbemServices *Services, IWbemContext *Context, IWbemProviderInitSink *Sink)
{
    ProviderLog("Initialize user=%ls flags=0x%lx namespace=%ls locale=%ls services=%d context=%d", User ? User : L"(null)",
                Flags, Namespace ? Namespace : L"(null)", Locale ? Locale : L"(null)", Services != NULL, Context != NULL);
    if (Services && !Provider.Namespace)
    {
        IWbemServices_AddRef(Services);
        Provider.Namespace = Services;
    }
    IWbemProviderInitSink_SetStatus(Sink, WBEM_S_INITIALIZED, 0);
    return WBEM_S_NO_ERROR;
}

static const IWbemProviderInitVtbl InitVtbl = { Init_QueryInterface, Init_AddRef, Init_Release, Init_Initialize };

static HRESULT
SpawnDynamic(const WCHAR *Class, IWbemContext *Context, LONG Id, IWbemClassObject **Instance)
{
    IWbemClassObject *Definition;
    BSTR Name = SysAllocString(Class);
    HRESULT hr;

    *Instance = NULL;
    hr = IWbemServices_GetObject(Provider.Namespace, Name, 0, Context, &Definition, NULL);
    SysFreeString(Name);
    if (FAILED(hr))
        return hr;
    hr = IWbemClassObject_SpawnInstance(Definition, 0, Instance);
    IWbemClassObject_Release(Definition);
    if (SUCCEEDED(hr))
        hr = WmiPutLong(*Instance, L"Id", Id);
    if (SUCCEEDED(hr))
        hr = WmiPutString(*Instance, L"Name", Id == 1 ? L"one" : L"two");
    return hr;
}

static HRESULT WINAPI Svc_QueryInterface(IWbemServices *This, REFIID Iid, void **Object) { return ProviderQueryInterface(Iid, Object); }
static ULONG WINAPI Svc_AddRef(IWbemServices *This) { return InterlockedIncrement(&Provider.Refs); }
static ULONG WINAPI Svc_Release(IWbemServices *This) { return InterlockedDecrement(&Provider.Refs); }

#define NOT_CAPABLE(Name, ...) \
    static HRESULT WINAPI Name(IWbemServices *This, __VA_ARGS__) { ProviderLog(#Name); return WBEM_E_PROVIDER_NOT_CAPABLE; }

NOT_CAPABLE(Svc_OpenNamespace, const BSTR a, LONG b, IWbemContext *c, IWbemServices **d, IWbemCallResult **e)
NOT_CAPABLE(Svc_CancelAsyncCall, IWbemObjectSink *a)
NOT_CAPABLE(Svc_QueryObjectSink, LONG a, IWbemObjectSink **b)
NOT_CAPABLE(Svc_GetObject, const BSTR a, LONG b, IWbemContext *c, IWbemClassObject **d, IWbemCallResult **e)
NOT_CAPABLE(Svc_PutClass, IWbemClassObject *a, LONG b, IWbemContext *c, IWbemCallResult **d)
NOT_CAPABLE(Svc_PutClassAsync, IWbemClassObject *a, LONG b, IWbemContext *c, IWbemObjectSink *d)
NOT_CAPABLE(Svc_DeleteClass, const BSTR a, LONG b, IWbemContext *c, IWbemCallResult **d)
NOT_CAPABLE(Svc_DeleteClassAsync, const BSTR a, LONG b, IWbemContext *c, IWbemObjectSink *d)
NOT_CAPABLE(Svc_CreateClassEnum, const BSTR a, LONG b, IWbemContext *c, IEnumWbemClassObject **d)
NOT_CAPABLE(Svc_CreateClassEnumAsync, const BSTR a, LONG b, IWbemContext *c, IWbemObjectSink *d)
NOT_CAPABLE(Svc_PutInstance, IWbemClassObject *a, LONG b, IWbemContext *c, IWbemCallResult **d)
NOT_CAPABLE(Svc_PutInstanceAsync, IWbemClassObject *a, LONG b, IWbemContext *c, IWbemObjectSink *d)
NOT_CAPABLE(Svc_DeleteInstance, const BSTR a, LONG b, IWbemContext *c, IWbemCallResult **d)
NOT_CAPABLE(Svc_DeleteInstanceAsync, const BSTR a, LONG b, IWbemContext *c, IWbemObjectSink *d)
NOT_CAPABLE(Svc_CreateInstanceEnum, const BSTR a, LONG b, IWbemContext *c, IEnumWbemClassObject **d)
NOT_CAPABLE(Svc_ExecQuery, const BSTR a, const BSTR b, LONG c, IWbemContext *d, IEnumWbemClassObject **e)
NOT_CAPABLE(Svc_ExecNotificationQuery, const BSTR a, const BSTR b, LONG c, IWbemContext *d, IEnumWbemClassObject **e)
NOT_CAPABLE(Svc_ExecNotificationQueryAsync, const BSTR a, const BSTR b, LONG c, IWbemContext *d, IWbemObjectSink *e)
NOT_CAPABLE(Svc_ExecMethod, const BSTR a, const BSTR b, LONG c, IWbemContext *d, IWbemClassObject *e,
            IWbemClassObject **f, IWbemCallResult **g)

static HRESULT WINAPI
Svc_GetObjectAsync(IWbemServices *This, const BSTR Path, LONG Flags, IWbemContext *Context, IWbemObjectSink *Sink)
{
    IWbemClassObject *Instance = NULL;
    const WCHAR *Equals = Path ? wcschr(Path, L'=') : NULL;
    LONG Id = Equals ? _wtol(Equals + 1) : 0;
    HRESULT hr = WBEM_E_NOT_FOUND;

    ProviderLog("GetObjectAsync path=%ls flags=0x%lx context=%d", Path ? Path : L"(null)", Flags, Context != NULL);
    if (Id == 1 || Id == 2)
        hr = SpawnDynamic(L"Lnt_Dynamic", Context, Id, &Instance);
    if (Instance)
    {
        IWbemObjectSink_Indicate(Sink, 1, &Instance);
        IWbemClassObject_Release(Instance);
    }
    IWbemObjectSink_SetStatus(Sink, WBEM_STATUS_COMPLETE, hr, NULL, NULL);
    return WBEM_S_NO_ERROR;
}

static HRESULT WINAPI
Svc_CreateInstanceEnumAsync(IWbemServices *This, const BSTR Class, LONG Flags, IWbemContext *Context,
                            IWbemObjectSink *Sink)
{
    IWbemClassObject *Instances[2] = { NULL, NULL };
    HRESULT hr;

    ProviderLog("CreateInstanceEnumAsync class=%ls flags=0x%lx context=%d", Class ? Class : L"(null)", Flags,
                Context != NULL);
    if (Class && !lstrcmpiW(Class, L"Lnt_Empty"))
    {
        IWbemObjectSink_SetStatus(Sink, WBEM_STATUS_COMPLETE, WBEM_E_NOT_FOUND, NULL, NULL);
        return WBEM_S_NO_ERROR;
    }
    hr = SpawnDynamic(L"Lnt_Dynamic", Context, 1, &Instances[0]);
    if (SUCCEEDED(hr))
        hr = SpawnDynamic(L"Lnt_Dynamic", Context, 2, &Instances[1]);
    if (SUCCEEDED(hr))
        IWbemObjectSink_Indicate(Sink, 2, Instances);
    if (Instances[0])
        IWbemClassObject_Release(Instances[0]);
    if (Instances[1])
        IWbemClassObject_Release(Instances[1]);
    IWbemObjectSink_SetStatus(Sink, WBEM_STATUS_COMPLETE, hr, NULL, NULL);
    return WBEM_S_NO_ERROR;
}

static HRESULT WINAPI
Svc_ExecQueryAsync(IWbemServices *This, const BSTR Language, const BSTR Query, LONG Flags, IWbemContext *Context,
                   IWbemObjectSink *Sink)
{
    ProviderLog("ExecQueryAsync query=%ls flags=0x%lx", Query ? Query : L"(null)", Flags);
    return WBEM_E_PROVIDER_NOT_CAPABLE;
}

static HRESULT WINAPI
Svc_ExecMethodAsync(IWbemServices *This, const BSTR Path, const BSTR Method, LONG Flags, IWbemContext *Context,
                    IWbemClassObject *InParams, IWbemObjectSink *Sink)
{
    IWbemClassObject *Definition = NULL, *OutSignature = NULL, *Out = NULL;
    BSTR Name = SysAllocString(L"Lnt_Dynamic");
    LONG X = InParams ? WmiGetLong(InParams, L"X", -1) : -1;
    HRESULT hr;

    ProviderLog("ExecMethodAsync path=%ls method=%ls flags=0x%lx X=%ld", Path ? Path : L"(null)",
                Method ? Method : L"(null)", Flags, X);
    hr = IWbemServices_GetObject(Provider.Namespace, Name, 0, Context, &Definition, NULL);
    SysFreeString(Name);
    if (SUCCEEDED(hr))
        hr = IWbemClassObject_GetMethod(Definition, L"Twice", 0, NULL, &OutSignature);
    if (SUCCEEDED(hr))
        hr = IWbemClassObject_SpawnInstance(OutSignature, 0, &Out);
    if (SUCCEEDED(hr))
        hr = WmiPutLong(Out, L"Y", 2 * X);
    if (SUCCEEDED(hr))
        hr = WmiPutLong(Out, L"ReturnValue", 0);
    if (SUCCEEDED(hr))
        IWbemObjectSink_Indicate(Sink, 1, &Out);
    IWbemObjectSink_SetStatus(Sink, WBEM_STATUS_COMPLETE, hr, NULL, NULL);
    if (Out)
        IWbemClassObject_Release(Out);
    if (OutSignature)
        IWbemClassObject_Release(OutSignature);
    if (Definition)
        IWbemClassObject_Release(Definition);
    return WBEM_S_NO_ERROR;
}

static const IWbemServicesVtbl SvcVtbl =
{
    Svc_QueryInterface, Svc_AddRef, Svc_Release, Svc_OpenNamespace, Svc_CancelAsyncCall, Svc_QueryObjectSink,
    Svc_GetObject, Svc_GetObjectAsync, Svc_PutClass, Svc_PutClassAsync, Svc_DeleteClass, Svc_DeleteClassAsync,
    Svc_CreateClassEnum, Svc_CreateClassEnumAsync, Svc_PutInstance, Svc_PutInstanceAsync, Svc_DeleteInstance,
    Svc_DeleteInstanceAsync, Svc_CreateInstanceEnum, Svc_CreateInstanceEnumAsync, Svc_ExecQuery, Svc_ExecQueryAsync,
    Svc_ExecNotificationQuery, Svc_ExecNotificationQueryAsync, Svc_ExecMethod, Svc_ExecMethodAsync
};

static void
RunProvider(const char *LogPath)
{
    IWbemDecoupledRegistrar *Registrar = NULL;
    HANDLE Ready, Stop;
    HRESULT hr;

    InitializeCriticalSection(&Provider.Lock);
    Provider.Log = fopen(LogPath, "w");
    if (!Provider.Log)
        return;
    Provider.IWbemProviderInit_iface.lpVtbl = &InitVtbl;
    Provider.IWbemServices_iface.lpVtbl = &SvcVtbl;
    Provider.Refs = 1;
    Ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, READY_EVENT);
    Stop = OpenEventA(SYNCHRONIZE, FALSE, STOP_EVENT);
    hr = WmiInitialize();
    ProviderLog("process initialize 0x%08lx", hr);
    hr = CoCreateInstance(&CLSID_WbemDecoupledRegistrar, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemDecoupledRegistrar,
                          (void **)&Registrar);
    ProviderLog("create registrar 0x%08lx", hr);
    if (SUCCEEDED(hr))
    {
        hr = IWbemDecoupledRegistrar_Register(Registrar, 0, NULL, NULL, NULL, PROV_NAMESPACE, PROV_NAME,
                                              (IUnknown *)&Provider.IWbemProviderInit_iface);
        ProviderLog("register 0x%08lx", hr);
    }
    if (Ready)
        SetEvent(Ready);
    if (Stop)
        WaitForSingleObject(Stop, 120000);
    if (Registrar)
    {
        hr = IWbemDecoupledRegistrar_UnRegister(Registrar);
        ProviderLog("unregister 0x%08lx", hr);
        IWbemDecoupledRegistrar_Release(Registrar);
    }
    if (Provider.Namespace)
        IWbemServices_Release(Provider.Namespace);
    ProviderLog("exit refs %ld", Provider.Refs);
    fclose(Provider.Log);
    CoUninitialize();
}

/* The provider ends every enumeration of Lnt_Empty with WBEM_E_NOT_FOUND: an empty result */
static void
TestFailedEnumeration(IWbemServices *Services)
{
    static const LONG Modes[] = { 0, WBEM_FLAG_FORWARD_ONLY, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY };
    BSTR Language = SysAllocString(L"WQL"), Query = SysAllocString(L"SELECT * FROM Lnt_Empty");
    BSTR Class = SysAllocString(L"Lnt_Empty");
    ULONG Index, Kind;

    for (Kind = 0; Kind < 2; Kind++)
    {
        for (Index = 0; Index < ARRAYSIZE(Modes); Index++)
        {
            IEnumWbemClassObject *Enum = NULL;
            HRESULT hr, Next = 0xdeadbeef;
            LONG Count = -1;

            if (Kind)
                hr = IWbemServices_ExecQuery(Services, Language, Query, Modes[Index], NULL, &Enum);
            else
                hr = IWbemServices_CreateInstanceEnum(Services, Class, Modes[Index], NULL, &Enum);
            if (Enum)
            {
                Count = WmiCount(Enum, &Next);
                IEnumWbemClassObject_Release(Enum);
            }
            winetest_push_context("%s flags 0x%lx", Kind ? "ExecQuery" : "CreateInstanceEnum", Modes[Index]);
            ok(hr == S_OK && Enum != NULL, "0x%08lx enum %p\n", hr, Enum);
            ok(Next == WBEM_S_FALSE && Count == 0, "next 0x%08lx count %ld\n", Next, Count);
            winetest_pop_context();
        }
    }
    SysFreeString(Language);
    SysFreeString(Query);
    SysFreeString(Class);
}

static void
TestQueries(IWbemServices *Services, BOOL Running)
{
    IWbemClassObject *Object = NULL, *Class = NULL, *InSignature = NULL, *In = NULL, *Out = NULL;
    BSTR Path, Method;
    LONG Count;
    HRESULT hr;

    hr = WmiEnumInstances(Services, L"Lnt_Dynamic", 0, &Count);
    if (!Running)
    {
        ok(hr == S_OK && Count == 0, "enumeration without provider 0x%08lx count %ld\n", hr, Count);
        hr = WmiGetObject(Services, L"Lnt_Dynamic.Id=1", &Object);
        ok(hr == WBEM_E_NOT_FOUND, "GetObject without provider 0x%08lx\n", hr);
        if (Object)
            IWbemClassObject_Release(Object);
        return;
    }
    ok(hr == S_OK && Count == 2, "enumeration 0x%08lx count %ld\n", hr, Count);

    hr = WmiGetObject(Services, L"Lnt_Dynamic.Id=2", &Object);
    ok(hr == S_OK, "GetObject(Id=2) 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        WCHAR Name[32];

        WmiGetString(Object, L"Name", Name, ARRAYSIZE(Name));
        ok(!lstrcmpW(Name, L"two"), "Name '%ls'\n", Name);
        WmiGetString(Object, L"__RELPATH", Name, ARRAYSIZE(Name));
        ok(!lstrcmpiW(Name, L"Lnt_Dynamic.Id=2"), "__RELPATH '%ls'\n", Name);
        IWbemClassObject_Release(Object);
    }
    hr = WmiGetObject(Services, L"Lnt_Dynamic.Id=9", &Object);
    ok(hr == WBEM_E_NOT_FOUND, "GetObject(Id=9) 0x%08lx\n", hr);

    hr = WmiQuery(Services, L"SELECT * FROM Lnt_Dynamic WHERE Id = 1", &Count);
    ok(hr == S_OK && Count == 1, "query 0x%08lx count %ld\n", hr, Count);

    hr = WmiGetObject(Services, L"Lnt_Dynamic", &Class);
    ok(hr == S_OK, "GetObject(Lnt_Dynamic) 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
        hr = IWbemClassObject_GetMethod(Class, L"Twice", 0, &InSignature, NULL);
    if (SUCCEEDED(hr))
        hr = IWbemClassObject_SpawnInstance(InSignature, 0, &In);
    if (SUCCEEDED(hr))
        hr = WmiPutLong(In, L"X", 21);
    ok(hr == S_OK, "method parameters 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Path = SysAllocString(L"Lnt_Dynamic.Id=1");
        Method = SysAllocString(L"Twice");
        hr = IWbemServices_ExecMethod(Services, Path, Method, 0, NULL, In, &Out, NULL);
        ok(hr == S_OK && Out, "ExecMethod 0x%08lx\n", hr);
        if (Out)
        {
            ok(WmiGetLong(Out, L"Y", -1) == 42, "Y %ld\n", WmiGetLong(Out, L"Y", -1));
            ok(WmiGetLong(Out, L"ReturnValue", -1) == 0, "ReturnValue %ld\n", WmiGetLong(Out, L"ReturnValue", -1));
            IWbemClassObject_Release(Out);
        }
        SysFreeString(Path);
        SysFreeString(Method);
    }
    if (In)
        IWbemClassObject_Release(In);
    if (InSignature)
        IWbemClassObject_Release(InSignature);
    if (Class)
        IWbemClassObject_Release(Class);

    TestFailedEnumeration(Services);
}

START_TEST(Decoupled)
{
    IWbemServices *Root, *Services;
    WBEM_COMPILE_STATUS_INFO Info;
    HANDLE Ready, Stop, Process = NULL;
    char **Argv, LogPath[MAX_PATH], Line[512];
    DWORD ExitCode = 0xFFFFFFFF;
    FILE *Log;
    HRESULT hr;

    if (winetest_get_mainargs(&Argv) >= 4 && !strcmp(Argv[2], "provider"))
    {
        RunProvider(Argv[3]);
        return;
    }
    hr = WmiInitialize();
    ok(hr == S_OK, "initialize 0x%08lx\n", hr);
    Root = WmiConnect(L"root\\cimv2", &hr);
    ok(Root != NULL, "connect root\\cimv2 0x%08lx\n", hr);
    if (!Root)
        return;
    WmiDeleteNamespace(Root, L"LntWmiProv");
    hr = WmiCreateNamespace(Root, L"LntWmiProv");
    ok(hr == S_OK, "create namespace 0x%08lx\n", hr);
    hr = WmiCompile(ProvMof, &Info);
    ok(hr == S_OK && Info.lPhaseError == 0, "compile 0x%08lx phase %ld hRes 0x%08lx object %ld\n", hr, Info.lPhaseError,
       Info.hRes, Info.ObjectNum);
    Services = WmiConnect(PROV_NAMESPACE, &hr);
    ok(Services != NULL, "connect 0x%08lx\n", hr);
    if (Services)
    {
        TestQueries(Services, FALSE);

        GetTempPathA(sizeof(LogPath), LogPath);
        lstrcatA(LogPath, "lnt_wmi_provider.log");
        Ready = CreateEventA(NULL, TRUE, FALSE, READY_EVENT);
        Stop = CreateEventA(NULL, TRUE, FALSE, STOP_EVENT);
        ExitCode = WmiRunSelf("Decoupled", "provider", LogPath, &Process);
        ok(ExitCode == 0 && Process, "start provider %lu\n", ExitCode);
        if (Process)
        {
            ok(WaitForSingleObject(Ready, 60000) == WAIT_OBJECT_0, "provider not ready\n");
            TestQueries(Services, TRUE);
            SetEvent(Stop);
            ok(WaitForSingleObject(Process, 60000) == WAIT_OBJECT_0, "provider did not exit\n");
            GetExitCodeProcess(Process, &ExitCode);
            ok(ExitCode == 0, "provider exit %lu\n", ExitCode);
            CloseHandle(Process);
            TestQueries(Services, FALSE);
        }
        CloseHandle(Ready);
        CloseHandle(Stop);
        Log = fopen(LogPath, "r");
        ok(Log != NULL, "no provider log\n");
        if (Log)
        {
            static const char *Expected[] =
            {
                "process initialize 0x00000000",
                "create registrar 0x00000000",
                "Initialize user=(null) flags=0x0 namespace=root\\cimv2\\LntWmiProv locale=(null) services=1 context=1",
                "register 0x00000000",
                "CreateInstanceEnumAsync class=Lnt_Dynamic flags=0x0 context=1",
                "GetObjectAsync path=Lnt_Dynamic.Id=2 flags=0x0 context=1",
                "GetObjectAsync path=Lnt_Dynamic.Id=9 flags=0x0 context=1",
                "CreateInstanceEnumAsync class=Lnt_Dynamic flags=0x0 context=1",
                "GetObjectAsync path=Lnt_Dynamic.Id=1 flags=0x0 context=1",
                "ExecMethodAsync path=Lnt_Dynamic.Id=1 method=Twice flags=0x0 X=21",
                "CreateInstanceEnumAsync class=Lnt_Empty flags=0x0 context=1",
                "CreateInstanceEnumAsync class=Lnt_Empty flags=0x0 context=1",
                "CreateInstanceEnumAsync class=Lnt_Empty flags=0x0 context=1",
                "CreateInstanceEnumAsync class=Lnt_Empty flags=0x0 context=1",
                "CreateInstanceEnumAsync class=Lnt_Empty flags=0x0 context=1",
                "CreateInstanceEnumAsync class=Lnt_Empty flags=0x0 context=1",
                "unregister 0x00000000",
                "exit refs 1",
            };
            ULONG Index = 0;

            while (fgets(Line, sizeof(Line), Log))
            {
                Line[strcspn(Line, "\r\n")] = 0;
                ok(Index < ARRAYSIZE(Expected) && !strcmp(Line, Expected[Index]), "provider call %lu: '%s', expected '%s'\n",
                   Index, Line, Index < ARRAYSIZE(Expected) ? Expected[Index] : "(end)");
                Index++;
            }
            ok(Index == ARRAYSIZE(Expected), "provider calls %lu, expected %u\n", Index, (unsigned)ARRAYSIZE(Expected));
            fclose(Log);
        }
        IWbemServices_Release(Services);
    }
    hr = WmiDeleteNamespace(Root, L"LntWmiProv");
    ok(hr == S_OK, "delete namespace 0x%08lx\n", hr);
    IWbemServices_Release(Root);
    CoUninitialize();
}
