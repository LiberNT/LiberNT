/*
 * PROJECT:     LiberNT WMI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Persistent repository of namespaces, classes and static instances
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

#define WMI_REPO_MAGIC 0x50524D4C
#define WMI_REPO_VERSION 1

typedef struct _WMI_ENTRY
{
    WCHAR *Namespace;
    IWbemClassObject *Object;
} WMI_ENTRY;

typedef struct _WMI_REPO
{
    ULONG NamespaceCount;
    WCHAR **Namespaces;
    ULONG ClassCount;
    WMI_ENTRY *Classes;
    ULONG InstanceCount;
    WMI_ENTRY *Instances;
    HANDLE Lock;
    BOOL Writable;
} WMI_REPO;

static const WCHAR *const BuiltinNamespaces[] =
{
    L"", L"cimv2", L"default", L"Microsoft\\Windows\\Storage", L"StandardCimv2", L"wmi"
};

BOOL
WmiRepoIsBuiltinNamespace(const WCHAR *Namespace)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(BuiltinNamespaces); Index++)
        if (!_wcsicmp(Namespace ? Namespace : L"", BuiltinNamespaces[Index]))
            return TRUE;
    return FALSE;
}

WCHAR *
WmiFullNamespace(const WCHAR *Namespace)
{
    SIZE_T Length = (Namespace ? wcslen(Namespace) : 0) + 6;
    WCHAR *Full = malloc(Length * sizeof(WCHAR));

    if (Full)
        swprintf(Full, Length, Namespace && *Namespace ? L"ROOT\\%ls" : L"ROOT", Namespace);
    return Full;
}

static BOOL
WmiRepoPaths(WCHAR *Data, WCHAR *Lock, WCHAR *Temporary)
{
    WCHAR Directory[MAX_PATH];
    UINT Length = GetSystemDirectoryW(Directory, MAX_PATH);

    if (!Length || Length > MAX_PATH - 64)
        return FALSE;
    wcscat(Directory, L"\\wbem\\Repository");
    CreateDirectoryW(Directory, NULL);
    swprintf(Data, MAX_PATH, L"%ls\\LiberNT.repository", Directory);
    swprintf(Lock, MAX_PATH, L"%ls\\LiberNT.repository.lock", Directory);
    swprintf(Temporary, MAX_PATH, L"%ls\\LiberNT.repository.new", Directory);
    return TRUE;
}

static void
WmiEntryFree(WMI_ENTRY *Entry)
{
    free(Entry->Namespace);
    if (Entry->Object)
        IWbemClassObject_Release(Entry->Object);
}

static void
WmiRepoClose(WMI_REPO *Repo)
{
    ULONG Index;

    for (Index = 0; Index < Repo->NamespaceCount; Index++)
        free(Repo->Namespaces[Index]);
    for (Index = 0; Index < Repo->ClassCount; Index++)
        WmiEntryFree(&Repo->Classes[Index]);
    for (Index = 0; Index < Repo->InstanceCount; Index++)
        WmiEntryFree(&Repo->Instances[Index]);
    free(Repo->Namespaces);
    free(Repo->Classes);
    free(Repo->Instances);
    if (Repo->Lock && Repo->Lock != INVALID_HANDLE_VALUE)
    {
        OVERLAPPED Overlapped = { 0 };

        UnlockFileEx(Repo->Lock, 0, 1, 0, &Overlapped);
        CloseHandle(Repo->Lock);
    }
    memset(Repo, 0, sizeof(*Repo));
}

static HRESULT
WmiRepoReadEntries(WMI_READER *Reader, ULONG *Count, WMI_ENTRY **Entries)
{
    ULONG Total = WmiReaderGetUlong(Reader), Index;

    if (Reader->Failed || Total > Reader->Size - Reader->Offset)
        return WBEM_E_INVALID_OBJECT;
    if (Total && !(*Entries = calloc(Total, sizeof(**Entries))))
        return WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; Index < Total; Index++)
    {
        WMI_ENTRY *Entry = &(*Entries)[Index];
        HRESULT hr;

        *Count = Index + 1;
        if (!(Entry->Namespace = WmiReaderGetString(Reader)))
            return WBEM_E_INVALID_OBJECT;
        hr = WmiObjectDeserialize(Reader, &Entry->Object);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* Opens the repository: a shared lock to read it, an exclusive one to change it */
static HRESULT
WmiRepoOpen(WMI_REPO *Repo, BOOL Write)
{
    WCHAR Data[MAX_PATH], LockPath[MAX_PATH], Temporary[MAX_PATH];
    OVERLAPPED Overlapped = { 0 };
    WMI_READER Reader = { 0 };
    LARGE_INTEGER Size;
    HANDLE File;
    BYTE *Buffer = NULL;
    DWORD Read = 0;
    ULONG Index;
    HRESULT hr = S_OK;

    memset(Repo, 0, sizeof(*Repo));
    if (!WmiRepoPaths(Data, LockPath, Temporary))
        return WBEM_E_FAILED;
    Repo->Lock = CreateFileW(LockPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (Repo->Lock == INVALID_HANDLE_VALUE)
    {
        if (Write)
            return WBEM_E_ACCESS_DENIED;
        Repo->Lock = CreateFileW(LockPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (Repo->Lock != INVALID_HANDLE_VALUE &&
        !LockFileEx(Repo->Lock, Write ? LOCKFILE_EXCLUSIVE_LOCK : 0, 0, 1, 0, &Overlapped))
    {
        CloseHandle(Repo->Lock);
        Repo->Lock = NULL;
        return WBEM_E_FAILED;
    }
    Repo->Writable = Write;
    File = CreateFileW(Data, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return S_OK;
    if (!GetFileSizeEx(File, &Size) || Size.QuadPart > 256 * 1024 * 1024 ||
        !(Buffer = malloc(max(Size.LowPart, 1))) || !ReadFile(File, Buffer, Size.LowPart, &Read, NULL) ||
        Read != Size.LowPart)
        hr = WBEM_E_FAILED;
    CloseHandle(File);
    if (SUCCEEDED(hr))
    {
        Reader.Data = Buffer;
        Reader.Size = Read;
        if (WmiReaderGetUlong(&Reader) != WMI_REPO_MAGIC || WmiReaderGetUlong(&Reader) != WMI_REPO_VERSION)
            hr = WBEM_E_INVALID_OBJECT;
    }
    if (SUCCEEDED(hr))
    {
        ULONG Count = WmiReaderGetUlong(&Reader);

        if (Reader.Failed || Count > Reader.Size)
            hr = WBEM_E_INVALID_OBJECT;
        else if (Count && !(Repo->Namespaces = calloc(Count, sizeof(WCHAR *))))
            hr = WBEM_E_OUT_OF_MEMORY;
        for (Index = 0; SUCCEEDED(hr) && Index < Count; Index++)
        {
            Repo->NamespaceCount = Index + 1;
            if (!(Repo->Namespaces[Index] = WmiReaderGetString(&Reader)))
                hr = WBEM_E_INVALID_OBJECT;
        }
    }
    if (SUCCEEDED(hr))
        hr = WmiRepoReadEntries(&Reader, &Repo->ClassCount, &Repo->Classes);
    if (SUCCEEDED(hr))
        hr = WmiRepoReadEntries(&Reader, &Repo->InstanceCount, &Repo->Instances);
    free(Buffer);
    if (FAILED(hr))
    {
        /* FIXME: an unreadable repository is treated as empty instead of being recovered from the autorecover list */
        HANDLE Lock = Repo->Lock;

        Repo->Lock = NULL;
        WmiRepoClose(Repo);
        Repo->Lock = Lock;
        Repo->Writable = Write;
    }
    return S_OK;
}

static void
WmiRepoWriteEntries(WMI_BUFFER *Buffer, ULONG Count, const WMI_ENTRY *Entries)
{
    ULONG Index;

    WmiBufferPutUlong(Buffer, Count);
    for (Index = 0; Index < Count; Index++)
    {
        WmiBufferPutString(Buffer, Entries[Index].Namespace);
        if (FAILED(WmiObjectSerialize(Entries[Index].Object, Buffer)))
            Buffer->Failed = TRUE;
    }
}

static HRESULT
WmiRepoSave(WMI_REPO *Repo)
{
    WCHAR Data[MAX_PATH], LockPath[MAX_PATH], Temporary[MAX_PATH];
    WMI_BUFFER Buffer = { 0 };
    HANDLE File;
    DWORD Written = 0;
    ULONG Index;
    HRESULT hr = S_OK;

    assert(Repo->Writable);
    if (!WmiRepoPaths(Data, LockPath, Temporary))
        return WBEM_E_FAILED;
    WmiBufferPutUlong(&Buffer, WMI_REPO_MAGIC);
    WmiBufferPutUlong(&Buffer, WMI_REPO_VERSION);
    WmiBufferPutUlong(&Buffer, Repo->NamespaceCount);
    for (Index = 0; Index < Repo->NamespaceCount; Index++)
        WmiBufferPutString(&Buffer, Repo->Namespaces[Index]);
    WmiRepoWriteEntries(&Buffer, Repo->ClassCount, Repo->Classes);
    WmiRepoWriteEntries(&Buffer, Repo->InstanceCount, Repo->Instances);
    if (Buffer.Failed)
    {
        WmiBufferFree(&Buffer);
        return WBEM_E_OUT_OF_MEMORY;
    }
    File = CreateFileW(Temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        hr = WBEM_E_ACCESS_DENIED;
    else
    {
        if (!WriteFile(File, Buffer.Data, (DWORD)Buffer.Size, &Written, NULL) || Written != Buffer.Size ||
            !FlushFileBuffers(File))
            hr = WBEM_E_FAILED;
        CloseHandle(File);
        if (SUCCEEDED(hr) && !MoveFileExW(Temporary, Data, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            hr = WBEM_E_FAILED;
        if (FAILED(hr))
            DeleteFileW(Temporary);
    }
    WmiBufferFree(&Buffer);
    return hr;
}

static LONG
WmiRepoFindNamespace(WMI_REPO *Repo, const WCHAR *Namespace)
{
    ULONG Index;

    for (Index = 0; Index < Repo->NamespaceCount; Index++)
        if (!_wcsicmp(Repo->Namespaces[Index], Namespace))
            return (LONG)Index;
    return -1;
}

static BOOL
WmiRepoHasNamespace(WMI_REPO *Repo, const WCHAR *Namespace)
{
    return WmiRepoIsBuiltinNamespace(Namespace) || WmiRepoFindNamespace(Repo, Namespace) >= 0;
}

/* The case the namespace was created with */
static const WCHAR *
WmiRepoCanonicalNamespace(WMI_REPO *Repo, const WCHAR *Namespace)
{
    LONG Index = WmiRepoFindNamespace(Repo, Namespace);
    ULONG Builtin;

    if (Index >= 0)
        return Repo->Namespaces[Index];
    for (Builtin = 0; Builtin < ARRAYSIZE(BuiltinNamespaces); Builtin++)
        if (!_wcsicmp(Namespace, BuiltinNamespaces[Builtin]))
            return BuiltinNamespaces[Builtin];
    return Namespace;
}

static void
WmiPlaceObject(IWbemClassObject *Object, const WCHAR *Namespace)
{
    WCHAR Computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD Length = ARRAYSIZE(Computer);
    WCHAR *Full = WmiFullNamespace(Namespace);

    if (!GetComputerNameW(Computer, &Length))
        wcscpy(Computer, L".");
    WmiObjectSetLocation(Object, Computer, Full);
    free(Full);
}

static HRESULT
WmiPlacedClone(IWbemClassObject *Source, const WCHAR *Namespace, IWbemClassObject **Result)
{
    HRESULT hr = IWbemClassObject_Clone(Source, Result);

    if (SUCCEEDED(hr))
        WmiPlaceObject(*Result, Namespace);
    return hr;
}

BOOL
WmiRepoNamespaceExists(const WCHAR *Namespace)
{
    WMI_REPO Repo;
    BOOL Exists;

    if (WmiRepoIsBuiltinNamespace(Namespace))
        return TRUE;
    if (FAILED(WmiRepoOpen(&Repo, FALSE)))
        return FALSE;
    Exists = WmiRepoFindNamespace(&Repo, Namespace) >= 0;
    WmiRepoClose(&Repo);
    return Exists;
}

static WCHAR *
WmiJoinNamespace(const WCHAR *Parent, const WCHAR *Name)
{
    SIZE_T Length = wcslen(Parent) + wcslen(Name) + 2;
    WCHAR *Full = malloc(Length * sizeof(WCHAR));

    if (Full)
        swprintf(Full, Length, *Parent ? L"%ls\\%ls" : L"%ls%ls", Parent, Name);
    return Full;
}

HRESULT
WmiRepoCreateNamespace(const WCHAR *Parent, const WCHAR *Name)
{
    WMI_REPO Repo;
    WCHAR *Full, **Grown;
    HRESULT hr;

    if (!Name || !*Name || wcschr(Name, L'\\') || wcschr(Name, L'/'))
        return WBEM_E_INVALID_NAMESPACE;
    hr = WmiRepoOpen(&Repo, TRUE);
    if (FAILED(hr))
        return hr;
    if (!(Full = WmiJoinNamespace(WmiRepoCanonicalNamespace(&Repo, Parent), Name)))
        hr = WBEM_E_OUT_OF_MEMORY;
    else if (!WmiRepoHasNamespace(&Repo, Parent))
        hr = WBEM_E_INVALID_NAMESPACE;
    else if (WmiRepoHasNamespace(&Repo, Full))
        hr = WBEM_E_ALREADY_EXISTS;
    else if (!(Grown = realloc(Repo.Namespaces, (Repo.NamespaceCount + 1) * sizeof(*Grown))))
        hr = WBEM_E_OUT_OF_MEMORY;
    else
    {
        Repo.Namespaces = Grown;
        Grown[Repo.NamespaceCount++] = Full;
        Full = NULL;
        hr = WmiRepoSave(&Repo);
    }
    free(Full);
    WmiRepoClose(&Repo);
    return hr;
}

static BOOL
WmiInNamespaceTree(const WCHAR *Namespace, const WCHAR *Root)
{
    SIZE_T Length = wcslen(Root);

    return !_wcsicmp(Namespace, Root) || (!_wcsnicmp(Namespace, Root, Length) && Namespace[Length] == L'\\');
}

static void
WmiRepoRemoveEntries(ULONG *Count, WMI_ENTRY *Entries, BOOL (*Remove)(WMI_ENTRY *, const void *), const void *Context)
{
    ULONG Read, Write = 0;

    for (Read = 0; Read < *Count; Read++)
    {
        if (Remove(&Entries[Read], Context))
            WmiEntryFree(&Entries[Read]);
        else
            Entries[Write++] = Entries[Read];
    }
    *Count = Write;
}

static BOOL
WmiEntryInTree(WMI_ENTRY *Entry, const void *Root)
{
    return WmiInNamespaceTree(Entry->Namespace, Root);
}

HRESULT
WmiRepoDeleteNamespace(const WCHAR *Parent, const WCHAR *Name)
{
    WMI_REPO Repo;
    WCHAR *Full;
    ULONG Read, Write = 0;
    HRESULT hr;

    hr = WmiRepoOpen(&Repo, TRUE);
    if (FAILED(hr))
        return hr;
    if (!(Full = WmiJoinNamespace(WmiRepoCanonicalNamespace(&Repo, Parent), Name)))
        hr = WBEM_E_OUT_OF_MEMORY;
    else if (WmiRepoIsBuiltinNamespace(Full))
        hr = WBEM_E_ACCESS_DENIED;
    else if (WmiRepoFindNamespace(&Repo, Full) < 0)
        hr = WBEM_E_NOT_FOUND;
    else
    {
        for (Read = 0; Read < Repo.NamespaceCount; Read++)
        {
            if (WmiInNamespaceTree(Repo.Namespaces[Read], Full))
                free(Repo.Namespaces[Read]);
            else
                Repo.Namespaces[Write++] = Repo.Namespaces[Read];
        }
        Repo.NamespaceCount = Write;
        WmiRepoRemoveEntries(&Repo.ClassCount, Repo.Classes, WmiEntryInTree, Full);
        WmiRepoRemoveEntries(&Repo.InstanceCount, Repo.Instances, WmiEntryInTree, Full);
        hr = WmiRepoSave(&Repo);
    }
    free(Full);
    WmiRepoClose(&Repo);
    return hr;
}

HRESULT
WmiRepoEnumNamespaces(const WCHAR *Parent, ULONG *Count, WCHAR ***Names)
{
    WMI_REPO Repo;
    SIZE_T Length = wcslen(Parent);
    ULONG Index;
    HRESULT hr;

    *Count = 0;
    *Names = NULL;
    hr = WmiRepoOpen(&Repo, FALSE);
    if (FAILED(hr))
        return hr;
    if (Repo.NamespaceCount && !(*Names = calloc(Repo.NamespaceCount, sizeof(WCHAR *))))
        hr = WBEM_E_OUT_OF_MEMORY;
    for (Index = 0; SUCCEEDED(hr) && Index < Repo.NamespaceCount; Index++)
    {
        const WCHAR *Name = Repo.Namespaces[Index], *Child;

        if (Length)
        {
            if (_wcsnicmp(Name, Parent, Length) || Name[Length] != L'\\')
                continue;
            Child = Name + Length + 1;
        }
        else
            Child = Name;
        if (wcschr(Child, L'\\'))
            continue;
        if (!((*Names)[(*Count)++] = _wcsdup(Child)))
            hr = WBEM_E_OUT_OF_MEMORY;
    }
    WmiRepoClose(&Repo);
    return hr;
}

static WMI_ENTRY *
WmiRepoFindClass(WMI_REPO *Repo, const WCHAR *Namespace, const WCHAR *Class)
{
    ULONG Index;

    for (Index = 0; Index < Repo->ClassCount; Index++)
        if (!_wcsicmp(Repo->Classes[Index].Namespace, Namespace) &&
            !_wcsicmp(WmiObjectClassName(Repo->Classes[Index].Object), Class))
            return &Repo->Classes[Index];
    return NULL;
}

/* System classes, from the documented class definitions */

typedef struct _WMI_SYSTEM_PROPERTY
{
    const WCHAR *Name;
    CIMTYPE Type;
    BOOL Key;
} WMI_SYSTEM_PROPERTY;

typedef struct _WMI_SYSTEM_CLASS
{
    const WCHAR *Name;
    const WCHAR *Super;
    const WMI_SYSTEM_PROPERTY *Properties;
    ULONG PropertyCount;
} WMI_SYSTEM_CLASS;

static const WMI_SYSTEM_PROPERTY NamespaceProperties[] = { { L"Name", CIM_STRING, TRUE } };
static const WMI_SYSTEM_PROPERTY ProviderProperties[] = { { L"Name", CIM_STRING, TRUE } };
static const WMI_SYSTEM_PROPERTY Win32ProviderProperties[] =
{
    { L"ClientLoadableCLSID", CIM_STRING }, { L"CLSID", CIM_STRING }, { L"Concurrency", CIM_SINT32 },
    { L"DefaultMachineName", CIM_STRING }, { L"Enabled", CIM_BOOLEAN }, { L"HostingModel", CIM_STRING },
    { L"ImpersonationLevel", CIM_SINT32 }, { L"InitializationReentrancy", CIM_SINT32 },
    { L"InitializationTimeoutInterval", CIM_DATETIME }, { L"InitializeAsAdminFirst", CIM_BOOLEAN },
    { L"OperationTimeoutInterval", CIM_DATETIME }, { L"PerLocaleInitialization", CIM_BOOLEAN },
    { L"PerUserInitialization", CIM_BOOLEAN }, { L"Pure", CIM_BOOLEAN }, { L"SecurityDescriptor", CIM_STRING },
    { L"SupportsExplicitShutdown", CIM_BOOLEAN }, { L"SupportsExtendedStatus", CIM_BOOLEAN },
    { L"SupportsQuotas", CIM_BOOLEAN }, { L"SupportsSendStatus", CIM_BOOLEAN }, { L"SupportsShutdown", CIM_BOOLEAN },
    { L"SupportsThrottling", CIM_BOOLEAN }, { L"UnloadTimeout", CIM_DATETIME }, { L"Version", CIM_UINT32 }
};
static const WMI_SYSTEM_PROPERTY RegistrationProperties[] = { { L"provider", CIM_REFERENCE, TRUE } };
static const WMI_SYSTEM_PROPERTY ObjectRegistrationProperties[] =
{
    { L"InteractionType", CIM_SINT32 }, { L"QuerySupportLevels", CIM_STRING | CIM_FLAG_ARRAY },
    { L"SupportsBatching", CIM_BOOLEAN }, { L"SupportsDelete", CIM_BOOLEAN }, { L"SupportsEnumeration", CIM_BOOLEAN },
    { L"SupportsGet", CIM_BOOLEAN }, { L"SupportsPut", CIM_BOOLEAN }, { L"SupportsTransactions", CIM_BOOLEAN }
};
static const WMI_SYSTEM_PROPERTY PropertyRegistrationProperties[] =
{
    { L"SupportsGet", CIM_BOOLEAN }, { L"SupportsPut", CIM_BOOLEAN }
};
static const WMI_SYSTEM_PROPERTY EventRegistrationProperties[] = { { L"EventQueryList", CIM_STRING | CIM_FLAG_ARRAY } };
static const WMI_SYSTEM_PROPERTY EventProperties[] =
{
    { L"SECURITY_DESCRIPTOR", CIM_UINT8 | CIM_FLAG_ARRAY }, { L"TIME_CREATED", CIM_UINT64 }
};

static const WMI_SYSTEM_CLASS SystemClasses[] =
{
    { L"__SystemClass", NULL, NULL, 0 },
    { L"__NAMESPACE", L"__SystemClass", NamespaceProperties, ARRAYSIZE(NamespaceProperties) },
    { L"__PARAMETERS", L"__SystemClass", NULL, 0 },
    { L"__Provider", L"__SystemClass", ProviderProperties, ARRAYSIZE(ProviderProperties) },
    { L"__Win32Provider", L"__Provider", Win32ProviderProperties, ARRAYSIZE(Win32ProviderProperties) },
    { L"__ProviderRegistration", L"__SystemClass", RegistrationProperties, ARRAYSIZE(RegistrationProperties) },
    { L"__ObjectProviderRegistration", L"__ProviderRegistration", ObjectRegistrationProperties,
      ARRAYSIZE(ObjectRegistrationProperties) },
    { L"__InstanceProviderRegistration", L"__ObjectProviderRegistration", NULL, 0 },
    { L"__ClassProviderRegistration", L"__ObjectProviderRegistration", NULL, 0 },
    { L"__MethodProviderRegistration", L"__ProviderRegistration", NULL, 0 },
    { L"__PropertyProviderRegistration", L"__ProviderRegistration", PropertyRegistrationProperties,
      ARRAYSIZE(PropertyRegistrationProperties) },
    { L"__EventProviderRegistration", L"__ProviderRegistration", EventRegistrationProperties,
      ARRAYSIZE(EventRegistrationProperties) },
    { L"__IndicationRelated", L"__SystemClass", NULL, 0 },
    { L"__Event", L"__IndicationRelated", EventProperties, ARRAYSIZE(EventProperties) },
    { L"__ExtrinsicEvent", L"__Event", NULL, 0 },
};

static const WMI_SYSTEM_CLASS *
WmiFindSystemClass(const WCHAR *Name)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(SystemClasses); Index++)
        if (!_wcsicmp(SystemClasses[Index].Name, Name))
            return &SystemClasses[Index];
    return NULL;
}

static HRESULT
WmiBuildSystemClass(const WMI_SYSTEM_CLASS *Definition, IWbemClassObject **Result)
{
    IWbemClassObject *Class = NULL;
    VARIANT Value;
    ULONG Index;
    HRESULT hr;

    *Result = NULL;
    if (Definition->Super)
    {
        IWbemClassObject *Base;

        hr = WmiBuildSystemClass(WmiFindSystemClass(Definition->Super), &Base);
        if (FAILED(hr))
            return hr;
        hr = IWbemClassObject_SpawnDerivedClass(Base, 0, &Class);
        IWbemClassObject_Release(Base);
    }
    else
        hr = WmiObjectCreateClass(&Class);
    if (FAILED(hr))
        return hr;
    V_VT(&Value) = VT_BSTR;
    V_BSTR(&Value) = SysAllocString(Definition->Name);
    hr = IWbemClassObject_Put(Class, L"__CLASS", 0, &Value, 0);
    VariantClear(&Value);
    for (Index = 0; SUCCEEDED(hr) && Index < Definition->PropertyCount; Index++)
    {
        const WMI_SYSTEM_PROPERTY *Property = &Definition->Properties[Index];

        hr = IWbemClassObject_Put(Class, Property->Name, 0, NULL, Property->Type);
        if (SUCCEEDED(hr) && Property->Key)
        {
            IWbemQualifierSet *Qualifiers;

            hr = IWbemClassObject_GetPropertyQualifierSet(Class, Property->Name, &Qualifiers);
            if (SUCCEEDED(hr))
            {
                V_VT(&Value) = VT_BOOL;
                V_BOOL(&Value) = VARIANT_TRUE;
                hr = IWbemQualifierSet_Put(Qualifiers, L"key", &Value, 0);
                IWbemQualifierSet_Release(Qualifiers);
            }
        }
    }
    if (FAILED(hr))
    {
        IWbemClassObject_Release(Class);
        return hr;
    }
    *Result = Class;
    return S_OK;
}

HRESULT
WmiRepoSystemClass(const WCHAR *Namespace, const WCHAR *Class, IWbemClassObject **Object)
{
    const WMI_SYSTEM_CLASS *Definition = WmiFindSystemClass(Class);
    HRESULT hr;

    *Object = NULL;
    if (!Definition)
        return WBEM_E_NOT_FOUND;
    hr = WmiBuildSystemClass(Definition, Object);
    if (SUCCEEDED(hr))
        WmiPlaceObject(*Object, Namespace);
    return hr;
}

static HRESULT
WmiRepoLookupClass(WMI_REPO *Repo, const WCHAR *Namespace, const WCHAR *Class, IWbemClassObject **Object)
{
    WMI_ENTRY *Entry = WmiRepoFindClass(Repo, Namespace, Class);

    if (Entry)
        return WmiPlacedClone(Entry->Object, Namespace, Object);
    return WmiRepoSystemClass(Namespace, Class, Object);
}

HRESULT
WmiRepoGetClass(const WCHAR *Namespace, const WCHAR *Class, IWbemClassObject **Object)
{
    WMI_REPO Repo;
    HRESULT hr;

    *Object = NULL;
    hr = WmiRepoOpen(&Repo, FALSE);
    if (FAILED(hr))
        return hr;
    hr = WmiRepoHasNamespace(&Repo, Namespace) ? WmiRepoLookupClass(&Repo, Namespace, Class, Object) :
                                                 WBEM_E_INVALID_NAMESPACE;
    WmiRepoClose(&Repo);
    return hr;
}

HRESULT
WmiRepoPutClass(const WCHAR *Namespace, IWbemClassObject *Class, LONG Flags)
{
    WMI_REPO Repo;
    WMI_ENTRY *Entry;
    IWbemClassObject *Copy = NULL, *Super = NULL;
    const WCHAR *Name, *SuperName;
    HRESULT hr;

    if (!WmiObjectIsOurs(Class) || WmiObjectGenus(Class) != WBEM_GENUS_CLASS)
        return WBEM_E_INVALID_PARAMETER;
    if (!(Name = WmiObjectClassName(Class)) || !*Name)
        return WBEM_E_INVALID_CLASS;
    if (Name[0] == L'_' && Name[1] == L'_')
        return WBEM_E_INVALID_OPERATION;
    hr = WmiRepoOpen(&Repo, TRUE);
    if (FAILED(hr))
        return hr;
    Entry = WmiRepoFindClass(&Repo, Namespace, Name);
    SuperName = WmiObjectSuperclass(Class);
    if (!WmiRepoHasNamespace(&Repo, Namespace))
        hr = WBEM_E_INVALID_NAMESPACE;
    else if (Entry && (Flags & WBEM_FLAG_CREATE_ONLY))
        hr = WBEM_E_ALREADY_EXISTS;
    else if (!Entry && (Flags & WBEM_FLAG_UPDATE_ONLY))
        hr = WBEM_E_NOT_FOUND;
    else if (SuperName && FAILED(WmiRepoLookupClass(&Repo, Namespace, SuperName, &Super)))
        hr = WBEM_E_INVALID_SUPERCLASS;
    if (Super)
        IWbemClassObject_Release(Super);
    if (SUCCEEDED(hr))
        hr = IWbemClassObject_Clone(Class, &Copy);
    if (SUCCEEDED(hr))
    {
        WmiObjectSetLocation(Copy, NULL, NULL);
        if (Entry)
        {
            /* TODO: refuse to change a class that has subclasses or instances unless the update mode allows it */
            IWbemClassObject_Release(Entry->Object);
            Entry->Object = Copy;
        }
        else
        {
            WMI_ENTRY *Grown = realloc(Repo.Classes, (Repo.ClassCount + 1) * sizeof(*Grown));
            WCHAR *Space = _wcsdup(WmiRepoCanonicalNamespace(&Repo, Namespace));

            if (!Grown || !Space)
            {
                if (Grown)
                    Repo.Classes = Grown;
                free(Space);
                IWbemClassObject_Release(Copy);
                hr = WBEM_E_OUT_OF_MEMORY;
            }
            else
            {
                Repo.Classes = Grown;
                Grown[Repo.ClassCount].Namespace = Space;
                Grown[Repo.ClassCount].Object = Copy;
                Repo.ClassCount++;
            }
        }
    }
    if (SUCCEEDED(hr))
        hr = WmiRepoSave(&Repo);
    WmiRepoClose(&Repo);
    return hr;
}

typedef struct _WMI_CLASS_MATCH
{
    const WCHAR *Namespace;
    const WCHAR *Class;
} WMI_CLASS_MATCH;

static BOOL
WmiEntryOfClassTree(WMI_ENTRY *Entry, const void *Context)
{
    const WMI_CLASS_MATCH *Match = Context;

    return !_wcsicmp(Entry->Namespace, Match->Namespace) && WmiObjectDerivesFrom(Entry->Object, Match->Class);
}

HRESULT
WmiRepoDeleteClass(const WCHAR *Namespace, const WCHAR *Class)
{
    WMI_REPO Repo;
    WMI_CLASS_MATCH Match = { Namespace, Class };
    HRESULT hr;

    if (Class[0] == L'_' && Class[1] == L'_')
        return WBEM_E_INVALID_OPERATION;
    hr = WmiRepoOpen(&Repo, TRUE);
    if (FAILED(hr))
        return hr;
    if (!WmiRepoFindClass(&Repo, Namespace, Class))
        hr = WBEM_E_NOT_FOUND;
    else
    {
        WmiRepoRemoveEntries(&Repo.ClassCount, Repo.Classes, WmiEntryOfClassTree, &Match);
        WmiRepoRemoveEntries(&Repo.InstanceCount, Repo.Instances, WmiEntryOfClassTree, &Match);
        hr = WmiRepoSave(&Repo);
    }
    WmiRepoClose(&Repo);
    return hr;
}

static HRESULT
WmiAppendObject(ULONG *Count, IWbemClassObject ***Objects, IWbemClassObject *Object)
{
    IWbemClassObject **Grown = realloc(*Objects, (*Count + 1) * sizeof(*Grown));

    if (!Grown)
    {
        IWbemClassObject_Release(Object);
        return WBEM_E_OUT_OF_MEMORY;
    }
    *Objects = Grown;
    Grown[(*Count)++] = Object;
    return S_OK;
}

void
WmiFreeObjects(ULONG Count, IWbemClassObject **Objects)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
        if (Objects[Index])
            IWbemClassObject_Release(Objects[Index]);
    free(Objects);
}

HRESULT
WmiRepoEnumClasses(const WCHAR *Namespace, const WCHAR *Superclass, BOOL Deep, ULONG *Count,
                   IWbemClassObject ***Classes)
{
    WMI_REPO Repo;
    ULONG Index;
    HRESULT hr;

    *Count = 0;
    *Classes = NULL;
    hr = WmiRepoOpen(&Repo, FALSE);
    if (FAILED(hr))
        return hr;
    if (!WmiRepoHasNamespace(&Repo, Namespace))
        hr = WBEM_E_INVALID_NAMESPACE;
    else if (Superclass && *Superclass && !WmiRepoFindClass(&Repo, Namespace, Superclass) &&
             !WmiFindSystemClass(Superclass))
        hr = WBEM_E_INVALID_CLASS;
    /* TODO: list the system classes along with the repository classes */
    for (Index = 0; SUCCEEDED(hr) && Index < Repo.ClassCount; Index++)
    {
        IWbemClassObject *Object = Repo.Classes[Index].Object, *Copy;
        const WCHAR *Super = WmiObjectSuperclass(Object);
        BOOL Selected;

        if (_wcsicmp(Repo.Classes[Index].Namespace, Namespace))
            continue;
        if (!Superclass || !*Superclass)
            Selected = Deep || !Super;
        else
            Selected = Deep ? (WmiObjectDerivesFrom(Object, Superclass) &&
                               _wcsicmp(WmiObjectClassName(Object), Superclass)) :
                              (Super && !_wcsicmp(Super, Superclass));
        if (!Selected)
            continue;
        hr = WmiPlacedClone(Object, Namespace, &Copy);
        if (SUCCEEDED(hr))
            hr = WmiAppendObject(Count, Classes, Copy);
    }
    WmiRepoClose(&Repo);
    if (FAILED(hr))
    {
        WmiFreeObjects(*Count, *Classes);
        *Count = 0;
        *Classes = NULL;
    }
    return hr;
}

static WMI_ENTRY *
WmiRepoFindInstance(WMI_REPO *Repo, const WCHAR *Namespace, const WMI_PATH *Path)
{
    ULONG Index;

    for (Index = 0; Index < Repo->InstanceCount; Index++)
    {
        WMI_ENTRY *Entry = &Repo->Instances[Index];

        if (!_wcsicmp(Entry->Namespace, Namespace) && WmiObjectDerivesFrom(Entry->Object, Path->Class) &&
            WmiPathMatchesInstance(Path, Entry->Object))
            return Entry;
    }
    return NULL;
}

static HRESULT
WmiRepoClassExists(WMI_REPO *Repo, const WCHAR *Namespace, const WCHAR *Class)
{
    return (WmiRepoFindClass(Repo, Namespace, Class) || WmiFindSystemClass(Class)) ? S_OK : WBEM_E_INVALID_CLASS;
}

HRESULT
WmiRepoPutInstance(const WCHAR *Namespace, IWbemClassObject *Instance, LONG Flags, WCHAR **RelativePath)
{
    WMI_REPO Repo;
    WMI_PATH Path;
    WMI_ENTRY *Entry;
    IWbemClassObject *Copy = NULL;
    WCHAR *Relative = NULL;
    HRESULT hr;

    if (RelativePath)
        *RelativePath = NULL;
    if (!WmiObjectIsOurs(Instance) || WmiObjectGenus(Instance) != WBEM_GENUS_INSTANCE)
        return WBEM_E_INVALID_PARAMETER;
    hr = WmiObjectRelativePath(Instance, &Relative);
    if (FAILED(hr))
        return hr == WBEM_E_INVALID_OBJECT ? WBEM_E_ILLEGAL_NULL : hr;
    hr = WmiParsePath(Relative, &Path);
    if (FAILED(hr))
    {
        free(Relative);
        return hr;
    }
    hr = WmiRepoOpen(&Repo, TRUE);
    if (SUCCEEDED(hr))
    {
        hr = WmiRepoHasNamespace(&Repo, Namespace) ? WmiRepoClassExists(&Repo, Namespace, Path.Class) :
                                                     WBEM_E_INVALID_NAMESPACE;
        Entry = SUCCEEDED(hr) ? WmiRepoFindInstance(&Repo, Namespace, &Path) : NULL;
        if (SUCCEEDED(hr) && Entry && (Flags & WBEM_FLAG_CREATE_ONLY))
            hr = WBEM_E_ALREADY_EXISTS;
        else if (SUCCEEDED(hr) && !Entry && (Flags & WBEM_FLAG_UPDATE_ONLY))
            hr = WBEM_E_NOT_FOUND;
        if (SUCCEEDED(hr))
            hr = IWbemClassObject_Clone(Instance, &Copy);
        if (SUCCEEDED(hr))
        {
            WmiObjectSetLocation(Copy, NULL, NULL);
            if (Entry)
            {
                IWbemClassObject_Release(Entry->Object);
                Entry->Object = Copy;
            }
            else
            {
                WMI_ENTRY *Grown = realloc(Repo.Instances, (Repo.InstanceCount + 1) * sizeof(*Grown));
                WCHAR *Space = _wcsdup(WmiRepoCanonicalNamespace(&Repo, Namespace));

                if (!Grown || !Space)
                {
                    if (Grown)
                        Repo.Instances = Grown;
                    free(Space);
                    IWbemClassObject_Release(Copy);
                    hr = WBEM_E_OUT_OF_MEMORY;
                }
                else
                {
                    Repo.Instances = Grown;
                    Grown[Repo.InstanceCount].Namespace = Space;
                    Grown[Repo.InstanceCount].Object = Copy;
                    Repo.InstanceCount++;
                }
            }
        }
        if (SUCCEEDED(hr))
            hr = WmiRepoSave(&Repo);
        WmiRepoClose(&Repo);
    }
    WmiFreePath(&Path);
    if (SUCCEEDED(hr) && RelativePath)
        *RelativePath = Relative;
    else
        free(Relative);
    return hr;
}

HRESULT
WmiRepoGetInstance(const WCHAR *Namespace, const WMI_PATH *Path, IWbemClassObject **Instance)
{
    WMI_REPO Repo;
    WMI_ENTRY *Entry;
    HRESULT hr;

    *Instance = NULL;
    hr = WmiRepoOpen(&Repo, FALSE);
    if (FAILED(hr))
        return hr;
    hr = WmiRepoHasNamespace(&Repo, Namespace) ? WmiRepoClassExists(&Repo, Namespace, Path->Class) :
                                                 WBEM_E_INVALID_NAMESPACE;
    if (SUCCEEDED(hr))
    {
        Entry = WmiRepoFindInstance(&Repo, Namespace, Path);
        hr = Entry ? WmiPlacedClone(Entry->Object, Namespace, Instance) : WBEM_E_NOT_FOUND;
    }
    WmiRepoClose(&Repo);
    return hr;
}

HRESULT
WmiRepoDeleteInstance(const WCHAR *Namespace, const WMI_PATH *Path)
{
    WMI_REPO Repo;
    WMI_ENTRY *Entry;
    HRESULT hr;

    hr = WmiRepoOpen(&Repo, TRUE);
    if (FAILED(hr))
        return hr;
    hr = WmiRepoHasNamespace(&Repo, Namespace) ? WmiRepoClassExists(&Repo, Namespace, Path->Class) :
                                                 WBEM_E_INVALID_NAMESPACE;
    if (SUCCEEDED(hr))
    {
        Entry = WmiRepoFindInstance(&Repo, Namespace, Path);
        if (!Entry)
            hr = WBEM_E_NOT_FOUND;
        else
        {
            ULONG Index = (ULONG)(Entry - Repo.Instances);

            WmiEntryFree(Entry);
            memmove(Entry, Entry + 1, (Repo.InstanceCount - Index - 1) * sizeof(*Entry));
            Repo.InstanceCount--;
            hr = WmiRepoSave(&Repo);
        }
    }
    WmiRepoClose(&Repo);
    return hr;
}

HRESULT
WmiRepoEnumInstances(const WCHAR *Namespace, const WCHAR *Class, BOOL Deep, ULONG *Count,
                     IWbemClassObject ***Instances)
{
    WMI_REPO Repo;
    ULONG Index;
    HRESULT hr;

    *Count = 0;
    *Instances = NULL;
    hr = WmiRepoOpen(&Repo, FALSE);
    if (FAILED(hr))
        return hr;
    hr = WmiRepoHasNamespace(&Repo, Namespace) ? WmiRepoClassExists(&Repo, Namespace, Class) :
                                                 WBEM_E_INVALID_NAMESPACE;
    for (Index = 0; SUCCEEDED(hr) && Index < Repo.InstanceCount; Index++)
    {
        WMI_ENTRY *Entry = &Repo.Instances[Index];
        IWbemClassObject *Copy;

        if (_wcsicmp(Entry->Namespace, Namespace))
            continue;
        if (Deep ? !WmiObjectDerivesFrom(Entry->Object, Class) : _wcsicmp(WmiObjectClassName(Entry->Object), Class))
            continue;
        hr = WmiPlacedClone(Entry->Object, Namespace, &Copy);
        if (SUCCEEDED(hr))
            hr = WmiAppendObject(Count, Instances, Copy);
    }
    WmiRepoClose(&Repo);
    if (FAILED(hr))
    {
        WmiFreeObjects(*Count, *Instances);
        *Count = 0;
        *Instances = NULL;
    }
    return hr;
}
