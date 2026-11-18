/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WMI repository namespaces, classes and static instances
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wmitest.h"

#define REPO_NAMESPACE L"root\\cimv2\\LntWmiRepo"

static const char RepoMof[] =
    "#pragma namespace(\"\\\\\\\\.\\\\root\\\\cimv2\\\\LntWmiRepo\")\n"
    "[Description(\"base class\")]\n"
    "class Lnt_Base\n"
    "{\n"
    "    [key] uint32 Id;\n"
    "    string Name = \"base\";\n"
    "};\n"
    "class Lnt_Derived : Lnt_Base\n"
    "{\n"
    "    [Description(\"signed value\")] sint32 Value;\n"
    "    uint64 Big;\n"
    "    string Tags[];\n"
    "    boolean Flag = TRUE;\n"
    "    real64 Ratio;\n"
    "    datetime When;\n"
    "    [Implemented] uint32 Twice([in] uint32 X, [out] uint32 Y);\n"
    "};\n"
    "instance of Lnt_Derived\n"
    "{\n"
    "    Id = 1;\n"
    "    Value = -5;\n"
    "    Big = 18446744073709551615;\n"
    "    Tags = {\"a\", \"b\"};\n"
    "    Ratio = 1.5;\n"
    "    When = \"20261118120000.000000+060\";\n"
    "};\n"
    "instance of Lnt_Derived\n"
    "{\n"
    "    Id = 2;\n"
    "    Name = \"two\";\n"
    "    Value = 7;\n"
    "    Flag = FALSE;\n"
    "};\n";

static void
CheckProperty(IWbemClassObject *Object, const WCHAR *Name, VARTYPE ExpectedVt, CIMTYPE ExpectedType, LONG ExpectedFlavor,
              int Line)
{
    VARIANT Value;
    CIMTYPE Type;
    LONG Flavor;
    HRESULT hr = WmiGet(Object, Name, &Value, &Type, &Flavor);

    ok_(__FILE__, Line)(hr == S_OK, "%ls: hr 0x%08lx\n", Name, hr);
    ok_(__FILE__, Line)(V_VT(&Value) == ExpectedVt, "%ls: vt %u, expected %u\n", Name, V_VT(&Value), ExpectedVt);
    ok_(__FILE__, Line)(Type == ExpectedType, "%ls: type 0x%lx, expected 0x%lx\n", Name, Type, ExpectedType);
    ok_(__FILE__, Line)(Flavor == ExpectedFlavor, "%ls: flavor 0x%lx, expected 0x%lx\n", Name, Flavor, ExpectedFlavor);
    VariantClear(&Value);
}

static void
CheckString(IWbemClassObject *Object, const WCHAR *Name, const WCHAR *Expected, int Line)
{
    WCHAR Buffer[260];
    BOOL Got = WmiGetString(Object, Name, Buffer, ARRAYSIZE(Buffer));

    ok_(__FILE__, Line)(Got && !lstrcmpiW(Buffer, Expected), "%ls: '%ls', expected '%ls'\n", Name, Got ? Buffer : L"(none)",
                        Expected);
    if (Got && lstrcmpW(Buffer, Expected))
        trace_(__FILE__, Line)("%ls differs in case: '%ls'\n", Name, Buffer);
}

static void
TestClass(IWbemServices *Services)
{
    IWbemClassObject *Class, *InSignature = NULL, *OutSignature = NULL;
    IWbemQualifierSet *Qualifiers;
    VARIANT Value;
    LONG Flavor;
    BSTR Origin = NULL;
    HRESULT hr;

    hr = WmiGetObject(Services, L"Lnt_Derived", &Class);
    ok(hr == S_OK, "GetObject(Lnt_Derived) 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    CheckString(Class, L"__CLASS", L"Lnt_Derived", __LINE__);
    CheckString(Class, L"__SUPERCLASS", L"Lnt_Base", __LINE__);
    CheckString(Class, L"__DYNASTY", L"Lnt_Base", __LINE__);
    CheckString(Class, L"__RELPATH", L"Lnt_Derived", __LINE__);
    CheckString(Class, L"__NAMESPACE", L"ROOT\\cimv2\\LntWmiRepo", __LINE__);
    ok(WmiGetLong(Class, L"__GENUS", -1) == WBEM_GENUS_CLASS, "__GENUS %ld\n", WmiGetLong(Class, L"__GENUS", -1));
    ok(WmiGetLong(Class, L"__PROPERTY_COUNT", -1) == 8, "__PROPERTY_COUNT %ld\n", WmiGetLong(Class, L"__PROPERTY_COUNT", -1));
    CheckProperty(Class, L"Id", VT_NULL, CIM_UINT32, WBEM_FLAVOR_ORIGIN_PROPAGATED, __LINE__);
    CheckProperty(Class, L"Name", VT_BSTR, CIM_STRING, WBEM_FLAVOR_ORIGIN_PROPAGATED, __LINE__);
    CheckProperty(Class, L"Value", VT_NULL, CIM_SINT32, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
    CheckProperty(Class, L"Big", VT_NULL, CIM_UINT64, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
    CheckProperty(Class, L"Tags", VT_NULL, CIM_STRING | CIM_FLAG_ARRAY, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
    CheckProperty(Class, L"Flag", VT_BOOL, CIM_BOOLEAN, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
    CheckProperty(Class, L"Ratio", VT_NULL, CIM_REAL64, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
    CheckProperty(Class, L"When", VT_NULL, CIM_DATETIME, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
    CheckString(Class, L"Name", L"base", __LINE__);

    hr = IWbemClassObject_GetPropertyOrigin(Class, L"Id", &Origin);
    ok(hr == S_OK && Origin && !lstrcmpW(Origin, L"Lnt_Base"), "origin of Id 0x%08lx %ls\n", hr, Origin ? Origin : L"");
    SysFreeString(Origin);
    Origin = NULL;
    hr = IWbemClassObject_GetPropertyOrigin(Class, L"Value", &Origin);
    ok(hr == S_OK && Origin && !lstrcmpW(Origin, L"Lnt_Derived"), "origin of Value 0x%08lx %ls\n", hr, Origin ? Origin : L"");
    SysFreeString(Origin);

    hr = IWbemClassObject_GetPropertyQualifierSet(Class, L"Id", &Qualifiers);
    ok(hr == S_OK, "GetPropertyQualifierSet(Id) 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        VariantInit(&Value);
        hr = IWbemQualifierSet_Get(Qualifiers, L"key", 0, &Value, &Flavor);
        ok(hr == S_OK && V_VT(&Value) == VT_BOOL && V_BOOL(&Value) == VARIANT_TRUE, "key qualifier 0x%08lx vt %u\n", hr,
           V_VT(&Value));
        ok(Flavor == (WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE | WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS |
                      WBEM_FLAVOR_NOT_OVERRIDABLE | WBEM_FLAVOR_ORIGIN_PROPAGATED), "key flavor 0x%lx\n", Flavor);
        VariantClear(&Value);
        VariantInit(&Value);
        hr = IWbemQualifierSet_Get(Qualifiers, L"CIMTYPE", 0, &Value, &Flavor);
        ok(hr == S_OK && V_VT(&Value) == VT_BSTR && !lstrcmpW(V_BSTR(&Value), L"uint32"), "CIMTYPE of Id 0x%08lx\n", hr);
        VariantClear(&Value);
        IWbemQualifierSet_Release(Qualifiers);
    }
    hr = IWbemClassObject_GetQualifierSet(Class, &Qualifiers);
    ok(hr == S_OK, "GetQualifierSet 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        VariantInit(&Value);
        hr = IWbemQualifierSet_Get(Qualifiers, L"Description", 0, &Value, &Flavor);
        ok(hr == WBEM_E_NOT_FOUND, "derived class Description 0x%08lx vt %u\n", hr, V_VT(&Value));
        VariantClear(&Value);
        IWbemQualifierSet_Release(Qualifiers);
    }

    hr = IWbemClassObject_GetMethod(Class, L"Twice", 0, &InSignature, &OutSignature);
    ok(hr == S_OK && InSignature && OutSignature, "GetMethod(Twice) 0x%08lx\n", hr);
    if (InSignature)
    {
        CheckProperty(InSignature, L"X", VT_NULL, CIM_UINT32, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
        IWbemClassObject_Release(InSignature);
    }
    if (OutSignature)
    {
        CheckProperty(OutSignature, L"Y", VT_NULL, CIM_UINT32, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
        CheckProperty(OutSignature, L"ReturnValue", VT_NULL, CIM_UINT32, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
        IWbemClassObject_Release(OutSignature);
    }
    IWbemClassObject_Release(Class);
}

static void
TestKeyFlavor(IWbemServices *Services)
{
    IWbemClassObject *Class = NULL, *Stored = NULL;
    IWbemQualifierSet *Qualifiers;
    VARIANT Value;
    LONG Flavor = -1;
    BSTR Name;
    HRESULT hr;

    hr = WmiGetObject(Services, NULL, &Class);
    ok(hr == S_OK && Class, "empty class 0x%08lx\n", hr);
    if (!Class)
        return;
    WmiPutString(Class, L"__CLASS", L"Lnt_KeyProbe");
    hr = IWbemClassObject_Put(Class, L"K", 0, NULL, CIM_UINT32);
    ok(hr == S_OK, "Put(K) 0x%08lx\n", hr);
    hr = IWbemClassObject_GetPropertyQualifierSet(Class, L"K", &Qualifiers);
    ok(hr == S_OK, "GetPropertyQualifierSet(K) 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        V_VT(&Value) = VT_BOOL;
        V_BOOL(&Value) = VARIANT_TRUE;
        hr = IWbemQualifierSet_Put(Qualifiers, L"key", &Value, 0);
        ok(hr == S_OK, "Put(key) 0x%08lx\n", hr);
        VariantInit(&Value);
        hr = IWbemQualifierSet_Get(Qualifiers, L"key", 0, &Value, &Flavor);
        ok(hr == S_OK && Flavor == (WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE | WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS |
                                    WBEM_FLAVOR_NOT_OVERRIDABLE), "key flavor before PutClass 0x%08lx 0x%lx\n", hr, Flavor);
        VariantClear(&Value);
        IWbemQualifierSet_Release(Qualifiers);
    }
    hr = IWbemServices_PutClass(Services, Class, WBEM_FLAG_CREATE_ONLY, NULL, NULL);
    ok(hr == S_OK, "PutClass(Lnt_KeyProbe) 0x%08lx\n", hr);
    hr = WmiGetObject(Services, L"Lnt_KeyProbe", &Stored);
    ok(hr == S_OK, "GetObject(Lnt_KeyProbe) 0x%08lx\n", hr);
    if (SUCCEEDED(hr) && SUCCEEDED(IWbemClassObject_GetPropertyQualifierSet(Stored, L"K", &Qualifiers)))
    {
        Flavor = -1;
        VariantInit(&Value);
        hr = IWbemQualifierSet_Get(Qualifiers, L"key", 0, &Value, &Flavor);
        ok(hr == S_OK && Flavor == (WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE | WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS |
                                    WBEM_FLAVOR_NOT_OVERRIDABLE), "stored key flavor 0x%08lx 0x%lx\n", hr, Flavor);
        VariantClear(&Value);
        IWbemQualifierSet_Release(Qualifiers);
    }
    if (Stored)
        IWbemClassObject_Release(Stored);
    IWbemClassObject_Release(Class);
    Name = SysAllocString(L"Lnt_KeyProbe");
    hr = IWbemServices_DeleteClass(Services, Name, 0, NULL, NULL);
    ok(hr == S_OK, "DeleteClass(Lnt_KeyProbe) 0x%08lx\n", hr);
    SysFreeString(Name);
}

static void
TestInstances(IWbemServices *Services)
{
    IWbemClassObject *Object, *Class, *Instance;
    VARIANT Value;
    CIMTYPE Type;
    LONG Count;
    BSTR Path;
    HRESULT hr;

    hr = WmiGetObject(Services, L"Lnt_Derived.Id=1", &Object);
    ok(hr == S_OK, "GetObject(Lnt_Derived.Id=1) 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(WmiGetLong(Object, L"__GENUS", -1) == WBEM_GENUS_INSTANCE, "__GENUS %ld\n", WmiGetLong(Object, L"__GENUS", -1));
        CheckString(Object, L"__RELPATH", L"Lnt_Derived.Id=1", __LINE__);
        CheckString(Object, L"Name", L"base", __LINE__);
        ok(WmiGetLong(Object, L"Value", 0) == -5, "Value %ld\n", WmiGetLong(Object, L"Value", 0));
        CheckProperty(Object, L"Value", VT_I4, CIM_SINT32, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
        CheckString(Object, L"Big", L"18446744073709551615", __LINE__);
        CheckProperty(Object, L"Big", VT_BSTR, CIM_UINT64, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
        CheckProperty(Object, L"Tags", VT_ARRAY | VT_BSTR, CIM_STRING | CIM_FLAG_ARRAY, WBEM_FLAVOR_ORIGIN_LOCAL, __LINE__);
        CheckString(Object, L"When", L"20261118120000.000000+060", __LINE__);
        hr = WmiGet(Object, L"Ratio", &Value, &Type, NULL);
        ok(hr == S_OK && V_VT(&Value) == VT_R8 && V_R8(&Value) == 1.5, "Ratio vt %u\n", V_VT(&Value));
        VariantClear(&Value);
        hr = WmiGet(Object, L"Flag", &Value, &Type, NULL);
        ok(hr == S_OK && V_VT(&Value) == VT_BOOL && V_BOOL(&Value) == VARIANT_TRUE, "Flag vt %u\n", V_VT(&Value));
        VariantClear(&Value);
        {
            WCHAR Text[260];

            WmiGetString(Object, L"__PATH", Text, ARRAYSIZE(Text));
            trace("__PATH %ls\n", Text);
        }
        IWbemClassObject_Release(Object);
    }

    hr = WmiGetObject(Services, L"Lnt_Base.Id=2", &Object);
    ok(hr == S_OK, "GetObject(Lnt_Base.Id=2) 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        CheckString(Object, L"__CLASS", L"Lnt_Derived", __LINE__);
        CheckString(Object, L"Name", L"two", __LINE__);
        IWbemClassObject_Release(Object);
    }

    hr = WmiGetObject(Services, L"Lnt_Derived.Id=99", &Object);
    ok(hr == WBEM_E_NOT_FOUND, "GetObject(missing instance) 0x%08lx\n", hr);
    hr = WmiGetObject(Services, L"Lnt_Nope", &Object);
    ok(hr == WBEM_E_NOT_FOUND, "GetObject(missing class) 0x%08lx\n", hr);

    hr = WmiEnumInstances(Services, L"Lnt_Base", WBEM_FLAG_DEEP, &Count);
    ok(hr == S_OK && Count == 2, "deep enumeration 0x%08lx count %ld\n", hr, Count);
    hr = WmiEnumInstances(Services, L"Lnt_Base", WBEM_FLAG_SHALLOW, &Count);
    ok(hr == S_OK && Count == 0, "shallow enumeration 0x%08lx count %ld\n", hr, Count);
    hr = WmiQuery(Services, L"SELECT * FROM Lnt_Derived WHERE Value > 0", &Count);
    ok(hr == S_OK && Count == 1, "query 0x%08lx count %ld\n", hr, Count);
    hr = WmiQuery(Services, L"SELECT Id FROM Lnt_Base", &Count);
    ok(hr == S_OK && Count == 2, "base query 0x%08lx count %ld\n", hr, Count);

    hr = WmiGetObject(Services, L"Lnt_Derived", &Class);
    ok(hr == S_OK, "GetObject(Lnt_Derived) 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IWbemClassObject_SpawnInstance(Class, 0, &Instance);
    ok(hr == S_OK, "SpawnInstance 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IWbemServices_PutInstance(Services, Instance, WBEM_FLAG_CREATE_ONLY, NULL, NULL);
        ok(hr == WBEM_E_ILLEGAL_NULL, "PutInstance without key 0x%08lx\n", hr);
        WmiPutLong(Instance, L"Id", 3);
        WmiPutLong(Instance, L"Value", 3);
        hr = IWbemServices_PutInstance(Services, Instance, WBEM_FLAG_CREATE_ONLY, NULL, NULL);
        ok(hr == S_OK, "PutInstance(create) 0x%08lx\n", hr);
        hr = IWbemServices_PutInstance(Services, Instance, WBEM_FLAG_CREATE_ONLY, NULL, NULL);
        ok(hr == WBEM_E_ALREADY_EXISTS, "PutInstance(create again) 0x%08lx\n", hr);
        WmiPutLong(Instance, L"Value", 33);
        hr = IWbemServices_PutInstance(Services, Instance, WBEM_FLAG_UPDATE_ONLY, NULL, NULL);
        ok(hr == S_OK, "PutInstance(update) 0x%08lx\n", hr);
        WmiPutLong(Instance, L"Id", 4);
        hr = IWbemServices_PutInstance(Services, Instance, WBEM_FLAG_UPDATE_ONLY, NULL, NULL);
        ok(hr == WBEM_E_NOT_FOUND, "PutInstance(update missing) 0x%08lx\n", hr);
        IWbemClassObject_Release(Instance);
    }
    IWbemClassObject_Release(Class);

    hr = WmiGetObject(Services, L"Lnt_Derived.Id=3", &Object);
    ok(hr == S_OK, "GetObject(Id=3) 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(WmiGetLong(Object, L"Value", 0) == 33, "Value %ld\n", WmiGetLong(Object, L"Value", 0));
        CheckString(Object, L"Name", L"base", __LINE__);
        IWbemClassObject_Release(Object);
    }

    Path = SysAllocString(L"Lnt_Derived.Id=1");
    hr = IWbemServices_DeleteInstance(Services, Path, 0, NULL, NULL);
    ok(hr == S_OK, "DeleteInstance 0x%08lx\n", hr);
    hr = IWbemServices_DeleteInstance(Services, Path, 0, NULL, NULL);
    ok(hr == WBEM_E_NOT_FOUND, "DeleteInstance again 0x%08lx\n", hr);
    SysFreeString(Path);
    hr = WmiGetObject(Services, L"Lnt_Derived.Id=1", &Object);
    ok(hr == WBEM_E_NOT_FOUND, "GetObject(deleted) 0x%08lx\n", hr);
}

static void
TestClassEnum(IWbemServices *Services)
{
    IEnumWbemClassObject *Enum;
    IWbemClassObject *Object;
    BSTR Super;
    ULONG Returned;
    BOOL SawBase = FALSE, SawDerived = FALSE;
    LONG Count = 0;
    WCHAR Name[128];
    HRESULT hr;

    hr = IWbemServices_CreateClassEnum(Services, NULL, WBEM_FLAG_SHALLOW, NULL, &Enum);
    ok(hr == S_OK, "CreateClassEnum(top) 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        while (IEnumWbemClassObject_Next(Enum, WBEM_INFINITE, 1, &Object, &Returned) == S_OK && Returned)
        {
            WmiGetString(Object, L"__CLASS", Name, ARRAYSIZE(Name));
            SawBase |= !lstrcmpW(Name, L"Lnt_Base");
            SawDerived |= !lstrcmpW(Name, L"Lnt_Derived");
            Count++;
            IWbemClassObject_Release(Object);
        }
        IEnumWbemClassObject_Release(Enum);
        ok(SawBase && !SawDerived, "top classes: base %d derived %d of %ld\n", SawBase, SawDerived, Count);
        trace("top-level classes in a new namespace: %ld\n", Count);
    }
    Super = SysAllocString(L"Lnt_Base");
    hr = IWbemServices_CreateClassEnum(Services, Super, WBEM_FLAG_SHALLOW, NULL, &Enum);
    SysFreeString(Super);
    ok(hr == S_OK, "CreateClassEnum(Lnt_Base) 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Count = WmiCount(Enum, NULL);
        ok(Count == 1, "subclasses of Lnt_Base %ld\n", Count);
        IEnumWbemClassObject_Release(Enum);
    }
}

static void
RunChild(void)
{
    IWbemServices *Services;
    IWbemClassObject *Object, *Class, *Instance;
    HRESULT hr;

    hr = WmiInitialize();
    ok(hr == S_OK, "child initialize 0x%08lx\n", hr);
    Services = WmiConnect(REPO_NAMESPACE, &hr);
    ok(Services != NULL, "child connect 0x%08lx\n", hr);
    if (!Services)
        return;
    hr = WmiGetObject(Services, L"Lnt_Derived.Id=3", &Object);
    ok(hr == S_OK, "child GetObject 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(WmiGetLong(Object, L"Value", 0) == 33, "child Value %ld\n", WmiGetLong(Object, L"Value", 0));
        IWbemClassObject_Release(Object);
    }
    hr = WmiGetObject(Services, L"Lnt_Derived", &Class);
    if (SUCCEEDED(hr) && SUCCEEDED(IWbemClassObject_SpawnInstance(Class, 0, &Instance)))
    {
        WmiPutLong(Instance, L"Id", 5);
        hr = IWbemServices_PutInstance(Services, Instance, WBEM_FLAG_CREATE_ONLY, NULL, NULL);
        ok(hr == S_OK, "child PutInstance 0x%08lx\n", hr);
        IWbemClassObject_Release(Instance);
    }
    if (Class)
        IWbemClassObject_Release(Class);
    IWbemServices_Release(Services);
    CoUninitialize();
}

START_TEST(Repository)
{
    IWbemServices *Root, *Services;
    IWbemClassObject *Object;
    WBEM_COMPILE_STATUS_INFO Info;
    char **Argv;
    DWORD ExitCode;
    BSTR Path;
    HRESULT hr;

    if (winetest_get_mainargs(&Argv) >= 3 && !strcmp(Argv[2], "child"))
    {
        RunChild();
        return;
    }
    hr = WmiInitialize();
    ok(hr == S_OK, "initialize 0x%08lx\n", hr);
    Root = WmiConnect(L"root\\cimv2", &hr);
    ok(Root != NULL, "connect root\\cimv2 0x%08lx\n", hr);
    if (!Root)
        return;
    WmiDeleteNamespace(Root, L"LntWmiRepo");
    Services = WmiConnect(REPO_NAMESPACE, &hr);
    ok(Services == NULL && hr == WBEM_E_INVALID_NAMESPACE, "connect before create 0x%08lx\n", hr);
    hr = WmiCreateNamespace(Root, L"LntWmiRepo");
    ok(hr == S_OK, "create namespace 0x%08lx\n", hr);
    hr = WmiCreateNamespace(Root, L"LntWmiRepo");
    ok(hr == WBEM_E_ALREADY_EXISTS, "create namespace again 0x%08lx\n", hr);
    hr = WmiGetObject(Root, L"__Namespace.Name=\"LntWmiRepo\"", &Object);
    ok(hr == S_OK, "GetObject(__Namespace) 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
        IWbemClassObject_Release(Object);
    Services = WmiConnect(REPO_NAMESPACE, &hr);
    ok(Services != NULL, "connect 0x%08lx\n", hr);
    if (Services)
    {

        hr = WmiCompile(RepoMof, &Info);
        ok(hr == S_OK && Info.lPhaseError == 0, "compile 0x%08lx phase %ld hRes 0x%08lx object %ld lines %ld-%ld\n", hr,
           Info.lPhaseError, Info.hRes, Info.ObjectNum, Info.FirstLine, Info.LastLine);

        TestClass(Services);
        TestKeyFlavor(Services);
        TestInstances(Services);
        TestClassEnum(Services);

        ExitCode = WmiRunSelf("Repository", "child", NULL, NULL);
        ok(ExitCode == 0, "child process exit %lu\n", ExitCode);
        hr = WmiGetObject(Services, L"Lnt_Derived.Id=5", &Object);
        ok(hr == S_OK, "GetObject(child instance) 0x%08lx\n", hr);
        if (SUCCEEDED(hr))
            IWbemClassObject_Release(Object);

        Path = SysAllocString(L"Lnt_Base");
        hr = IWbemServices_DeleteClass(Services, Path, 0, NULL, NULL);
        SysFreeString(Path);
        ok(hr == S_OK, "DeleteClass 0x%08lx\n", hr);
        hr = WmiGetObject(Services, L"Lnt_Derived", &Object);
        ok(hr == WBEM_E_NOT_FOUND, "GetObject(derived of deleted class) 0x%08lx\n", hr);
        hr = WmiGetObject(Services, L"Lnt_Derived.Id=2", &Object);
        ok(hr == WBEM_E_INVALID_CLASS, "GetObject(instance of deleted class) 0x%08lx\n", hr);
        IWbemServices_Release(Services);
    }

    hr = WmiDeleteNamespace(Root, L"LntWmiRepo");
    ok(hr == S_OK, "delete namespace 0x%08lx\n", hr);
    Services = WmiConnect(REPO_NAMESPACE, &hr);
    ok(Services == NULL && hr == WBEM_E_INVALID_NAMESPACE, "connect after delete 0x%08lx\n", hr);
    if (Services)
        IWbemServices_Release(Services);
    IWbemServices_Release(Root);
    CoUninitialize();
}
