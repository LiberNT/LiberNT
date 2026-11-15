/*
 * PROJECT:     LiberNT ADSI WinNT provider
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WinNT:// namespace for the local computer's groups and users
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <objbase.h>
#include <oleauto.h>
#include <iads.h>
#include <adserr.h>
#include <lm.h>
#include <sddl.h>
#include <strsafe.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(adsnt);

/* The WinNT Namespace Object class Windows 11 registers for the WinNT provider */
static const CLSID CLSID_WinNTNamespaceObject = {0x250e91a0, 0x0367, 0x11cf, {0xab, 0xc4, 0x02, 0x60, 0x8c, 0x9e, 0x75, 0x53}};
/* The WinNT Provider Object class Windows 11 registers under the WinNT ProgID */
static const CLSID CLSID_WinNTProviderObject = {0x8b20cd60, 0x0f29, 0x11cf, {0xab, 0xc4, 0x02, 0x60, 0x8c, 0x9e, 0x75, 0x53}};
static const GUID LIBID_ActiveDsTypeLibrary = {0x97d25db0, 0x0363, 0x11cf, {0xab, 0xc4, 0x02, 0x60, 0x8c, 0x9e, 0x75, 0x53}};

static const WCHAR WinNTPrefix[] = L"WinNT://";

typedef enum
{
    ObjectGroup,
    ObjectUser,
    ObjectComputer
} OBJECT_TYPE;

typedef struct
{
    IADsGroup IADsGroup_iface;
    IADsContainer IADsContainer_iface;
    LONG ref;
    OBJECT_TYPE type;
    WCHAR name[UNLEN + 1];
    WCHAR container[MAX_COMPUTERNAME_LENGTH + 1];
} ADS_OBJECT;

typedef struct
{
    IADsOpenDSObject IADsOpenDSObject_iface;
    LONG ref;
} NAMESPACE;

typedef struct
{
    IParseDisplayName IParseDisplayName_iface;
    LONG ref;
} PROVIDER;

static HRESULT get_type_info(REFIID iid, ITypeInfo **info)
{
    ITypeLib *lib;
    HRESULT hr;

    *info = NULL;
    hr = LoadRegTypeLib(&LIBID_ActiveDsTypeLibrary, 1, 0, LOCALE_SYSTEM_DEFAULT, &lib);
    if (FAILED(hr))
        return hr;
    hr = ITypeLib_GetTypeInfoOfGuid(lib, iid, info);
    ITypeLib_Release(lib);
    return hr;
}

static HRESULT status_to_hresult(NET_API_STATUS status)
{
    return status == NERR_Success ? S_OK : HRESULT_FROM_WIN32(status);
}

static BOOL is_local_container(PCWSTR container)
{
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = ARRAYSIZE(computer);

    return GetComputerNameW(computer, &size) && !lstrcmpiW(container, computer);
}

/* WinNT://container/name[,class] or WinNT://container/computer/name[,class]; the container is the local computer */
static HRESULT parse_path(PCWSTR path, WCHAR *container, WCHAR *name, WCHAR *cls, SIZE_T cls_size)
{
    PCWSTR rest, slash, last, comma;
    SIZE_T len;

    container[0] = name[0] = cls[0] = 0;
    if (!path || _wcsnicmp(path, WinNTPrefix, ARRAYSIZE(WinNTPrefix) - 1))
        return E_ADS_BAD_PATHNAME;

    rest = path + ARRAYSIZE(WinNTPrefix) - 1;
    slash = wcschr(rest, '/');
    comma = slash ? NULL : wcschr(rest, ',');
    len = slash ? (SIZE_T)(slash - rest) : comma ? (SIZE_T)(comma - rest) : wcslen(rest);
    if (!len || len > MAX_COMPUTERNAME_LENGTH)
        return E_ADS_BAD_PATHNAME;
    memcpy(container, rest, len * sizeof(WCHAR));
    container[len] = 0;

    if (!slash)
    {
        if (comma)
        {
            if (wcslen(comma + 1) >= cls_size)
                return E_ADS_BAD_PATHNAME;
            lstrcpyW(cls, comma + 1);
        }
        /* TODO: a domain or another computer */
        if (!is_local_container(container))
            return HRESULT_FROM_WIN32(ERROR_BAD_NETPATH);
        return S_OK;
    }

    last = wcsrchr(slash, '/') + 1;
    comma = wcschr(last, ',');
    len = comma ? (SIZE_T)(comma - last) : wcslen(last);
    if (!len || len > UNLEN)
        return E_ADS_BAD_PATHNAME;
    memcpy(name, last, len * sizeof(WCHAR));
    name[len] = 0;

    if (comma)
    {
        if (wcslen(comma + 1) >= cls_size)
            return E_ADS_BAD_PATHNAME;
        lstrcpyW(cls, comma + 1);
    }

    /* TODO: objects of a domain or of another computer */
    if (!is_local_container(container))
        return HRESULT_FROM_WIN32(ERROR_BAD_NETPATH);

    return S_OK;
}

/* A member is named by its WinNT path or by WinNT://S-1-... */
static HRESULT member_sid(BSTR path, PSID *sid)
{
    WCHAR container[MAX_COMPUTERNAME_LENGTH + 1], name[UNLEN + 1], cls[32];
    WCHAR account[MAX_COMPUTERNAME_LENGTH + UNLEN + 2] = {0};
    BYTE buffer[SECURITY_MAX_SID_SIZE];
    WCHAR domain[DNLEN + 1];
    DWORD sid_size = sizeof(buffer), domain_size = ARRAYSIZE(domain);
    SID_NAME_USE use;
    HRESULT hr;

    *sid = NULL;
    if (path && !_wcsnicmp(path, WinNTPrefix, ARRAYSIZE(WinNTPrefix) - 1) &&
        !_wcsnicmp(path + ARRAYSIZE(WinNTPrefix) - 1, L"S-1-", 4) && !wcschr(path + ARRAYSIZE(WinNTPrefix) - 1, '/'))
    {
        return ConvertStringSidToSidW(path + ARRAYSIZE(WinNTPrefix) - 1, sid) ? S_OK : E_ADS_BAD_PATHNAME;
    }

    hr = parse_path(path, container, name, cls, ARRAYSIZE(cls));
    if (FAILED(hr))
        return hr;
    if (!name[0])
        return E_ADS_BAD_PATHNAME;

    _snwprintf(account, ARRAYSIZE(account) - 1, L"%s\\%s", container, name);
    if (!LookupAccountNameW(NULL, account, buffer, &sid_size, domain, &domain_size, &use))
        return HRESULT_FROM_WIN32(GetLastError());

    *sid = LocalAlloc(LMEM_FIXED, sid_size);
    if (!*sid)
        return E_OUTOFMEMORY;
    CopySid(sid_size, *sid, buffer);
    return S_OK;
}

static inline ADS_OBJECT *impl_from_IADsGroup(IADsGroup *iface)
{
    return CONTAINING_RECORD(iface, ADS_OBJECT, IADsGroup_iface);
}

static HRESULT WINAPI object_QueryInterface(IADsGroup *iface, REFIID riid, void **obj)
{
    ADS_OBJECT *object = impl_from_IADsGroup(iface);

    *obj = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDispatch) || IsEqualGUID(riid, &IID_IADs) ||
        (object->type == ObjectGroup && IsEqualGUID(riid, &IID_IADsGroup)))
    {
        *obj = iface;
        IADsGroup_AddRef(iface);
        return S_OK;
    }
    if (object->type == ObjectComputer && IsEqualGUID(riid, &IID_IADsContainer))
    {
        *obj = &object->IADsContainer_iface;
        IADsGroup_AddRef(iface);
        return S_OK;
    }

    /* TODO: IADsUser, IADsComputer, IADsComputerOperations and IADsPropertyList */
    FIXME("unsupported interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI object_AddRef(IADsGroup *iface)
{
    return InterlockedIncrement(&impl_from_IADsGroup(iface)->ref);
}

static ULONG WINAPI object_Release(IADsGroup *iface)
{
    ADS_OBJECT *object = impl_from_IADsGroup(iface);
    LONG ref = InterlockedDecrement(&object->ref);

    if (!ref)
        HeapFree(GetProcessHeap(), 0, object);
    return ref;
}

static HRESULT WINAPI object_GetTypeInfoCount(IADsGroup *iface, UINT *count)
{
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI object_GetTypeInfo(IADsGroup *iface, UINT index, LCID lcid, ITypeInfo **info)
{
    ADS_OBJECT *object = impl_from_IADsGroup(iface);

    if (index)
        return DISP_E_BADINDEX;
    return get_type_info(object->type == ObjectGroup ? &IID_IADsGroup : &IID_IADs, info);
}

static HRESULT WINAPI object_GetIDsOfNames(IADsGroup *iface, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                           DISPID *ids)
{
    ITypeInfo *info;
    HRESULT hr;

    hr = object_GetTypeInfo(iface, 0, lcid, &info);
    if (FAILED(hr))
        return hr;
    hr = ITypeInfo_GetIDsOfNames(info, names, count, ids);
    ITypeInfo_Release(info);
    return hr;
}

static HRESULT WINAPI object_Invoke(IADsGroup *iface, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                    DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg)
{
    ITypeInfo *info;
    HRESULT hr;

    hr = object_GetTypeInfo(iface, 0, lcid, &info);
    if (FAILED(hr))
        return hr;
    hr = ITypeInfo_Invoke(info, iface, id, flags, params, result, excep, arg);
    ITypeInfo_Release(info);
    return hr;
}

static HRESULT WINAPI object_get_Name(IADsGroup *iface, BSTR *retval)
{
    *retval = SysAllocString(impl_from_IADsGroup(iface)->name);
    return *retval ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI object_get_Class(IADsGroup *iface, BSTR *retval)
{
    static const PCWSTR classes[] = { L"Group", L"User", L"Computer" };

    *retval = SysAllocString(classes[impl_from_IADsGroup(iface)->type]);
    return *retval ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI object_get_GUID(IADsGroup *iface, BSTR *retval)
{
    /* TODO: the class GUIDs of the WinNT schema */
    *retval = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI object_get_ADsPath(IADsGroup *iface, BSTR *retval)
{
    ADS_OBJECT *object = impl_from_IADsGroup(iface);
    WCHAR path[ARRAYSIZE(WinNTPrefix) + MAX_COMPUTERNAME_LENGTH + UNLEN + 2] = {0};

    if (object->type == ObjectComputer)
        _snwprintf(path, ARRAYSIZE(path) - 1, L"%s%s", WinNTPrefix, object->container);
    else
        _snwprintf(path, ARRAYSIZE(path) - 1, L"%s%s/%s", WinNTPrefix, object->container, object->name);
    *retval = SysAllocString(path);
    return *retval ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI object_get_Parent(IADsGroup *iface, BSTR *retval)
{
    ADS_OBJECT *object = impl_from_IADsGroup(iface);
    WCHAR path[ARRAYSIZE(WinNTPrefix) + MAX_COMPUTERNAME_LENGTH + 1] = {0};

    if (object->type == ObjectComputer)
        lstrcpyW(path, L"WinNT:");
    else
        _snwprintf(path, ARRAYSIZE(path) - 1, L"%s%s", WinNTPrefix, object->container);
    *retval = SysAllocString(path);
    return *retval ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI object_get_Schema(IADsGroup *iface, BSTR *retval)
{
    /* TODO: the WinNT schema objects */
    *retval = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI object_GetInfo(IADsGroup *iface)
{
    return S_OK;
}

static HRESULT WINAPI object_SetInfo(IADsGroup *iface)
{
    return S_OK;
}

static HRESULT WINAPI object_Get(IADsGroup *iface, BSTR name, VARIANT *prop)
{
    ADS_OBJECT *object = impl_from_IADsGroup(iface);

    VariantInit(prop);
    if (name && !lstrcmpiW(name, L"Name"))
    {
        V_VT(prop) = VT_BSTR;
        return object_get_Name(iface, &V_BSTR(prop));
    }
    if (name && !lstrcmpiW(name, L"Description") && object->type == ObjectGroup)
    {
        V_VT(prop) = VT_BSTR;
        return IADsGroup_get_Description(iface, &V_BSTR(prop));
    }

    /* TODO: the other properties of the WinNT schema */
    return E_ADS_PROPERTY_NOT_FOUND;
}

static HRESULT WINAPI object_Put(IADsGroup *iface, BSTR name, VARIANT prop)
{
    /* TODO: writing properties */
    return E_NOTIMPL;
}

static HRESULT WINAPI object_GetEx(IADsGroup *iface, BSTR name, VARIANT *prop)
{
    return object_Get(iface, name, prop);
}

static HRESULT WINAPI object_PutEx(IADsGroup *iface, LONG code, BSTR name, VARIANT prop)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI object_GetInfoEx(IADsGroup *iface, VARIANT props, LONG reserved)
{
    return S_OK;
}

static HRESULT WINAPI group_get_Description(IADsGroup *iface, BSTR *retval)
{
    ADS_OBJECT *object = impl_from_IADsGroup(iface);
    LOCALGROUP_INFO_1 *info = NULL;
    NET_API_STATUS status;

    *retval = NULL;
    status = NetLocalGroupGetInfo(NULL, object->name, 1, (LPBYTE *)&info);
    if (status != NERR_Success)
        return status_to_hresult(status);
    *retval = SysAllocString(info->lgrpi1_comment ? info->lgrpi1_comment : L"");
    NetApiBufferFree(info);
    return *retval ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI group_put_Description(IADsGroup *iface, BSTR value)
{
    ADS_OBJECT *object = impl_from_IADsGroup(iface);
    LOCALGROUP_INFO_1002 info = { value };

    return status_to_hresult(NetLocalGroupSetInfo(NULL, object->name, 1002, (LPBYTE)&info, NULL));
}

static HRESULT WINAPI group_Members(IADsGroup *iface, IADsMembers **members)
{
    /* TODO: the members collection */
    *members = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI group_IsMember(IADsGroup *iface, BSTR member, VARIANT_BOOL *is_member)
{
    ADS_OBJECT *object = impl_from_IADsGroup(iface);
    LOCALGROUP_MEMBERS_INFO_0 *members = NULL;
    DWORD read = 0, total = 0, i;
    NET_API_STATUS status;
    PSID sid;
    HRESULT hr;

    *is_member = VARIANT_FALSE;
    hr = member_sid(member, &sid);
    if (FAILED(hr))
        return hr;

    status = NetLocalGroupGetMembers(NULL, object->name, 0, (LPBYTE *)&members, MAX_PREFERRED_LENGTH, &read, &total, NULL);
    for (i = 0; status == NERR_Success && i < read; i++)
    {
        if (EqualSid(members[i].lgrmi0_sid, sid))
            *is_member = VARIANT_TRUE;
    }
    if (members)
        NetApiBufferFree(members);
    LocalFree(sid);
    return status_to_hresult(status);
}

static HRESULT change_member(ADS_OBJECT *object, BSTR member, BOOL add)
{
    LOCALGROUP_MEMBERS_INFO_0 info;
    NET_API_STATUS status;
    PSID sid;
    HRESULT hr;

    hr = member_sid(member, &sid);
    if (FAILED(hr))
        return hr;

    info.lgrmi0_sid = sid;
    status = add ? NetLocalGroupAddMembers(NULL, object->name, 0, (LPBYTE)&info, 1)
                 : NetLocalGroupDelMembers(NULL, object->name, 0, (LPBYTE)&info, 1);
    LocalFree(sid);
    TRACE("%s %s %s: %lu\n", add ? "add" : "remove", debugstr_w(member), debugstr_w(object->name), status);
    return status_to_hresult(status);
}

static HRESULT WINAPI group_Add(IADsGroup *iface, BSTR member)
{
    return change_member(impl_from_IADsGroup(iface), member, TRUE);
}

static HRESULT WINAPI group_Remove(IADsGroup *iface, BSTR member)
{
    return change_member(impl_from_IADsGroup(iface), member, FALSE);
}

static const IADsGroupVtbl ObjectVtbl =
{
    object_QueryInterface,
    object_AddRef,
    object_Release,
    object_GetTypeInfoCount,
    object_GetTypeInfo,
    object_GetIDsOfNames,
    object_Invoke,
    object_get_Name,
    object_get_Class,
    object_get_GUID,
    object_get_ADsPath,
    object_get_Parent,
    object_get_Schema,
    object_GetInfo,
    object_SetInfo,
    object_Get,
    object_Put,
    object_GetEx,
    object_PutEx,
    object_GetInfoEx,
    group_get_Description,
    group_put_Description,
    group_Members,
    group_IsMember,
    group_Add,
    group_Remove
};

static HRESULT open_object(PCWSTR path, IDispatch **disp);

static inline ADS_OBJECT *impl_from_IADsContainer(IADsContainer *iface)
{
    return CONTAINING_RECORD(iface, ADS_OBJECT, IADsContainer_iface);
}

static HRESULT WINAPI container_QueryInterface(IADsContainer *iface, REFIID riid, void **obj)
{
    return IADsGroup_QueryInterface(&impl_from_IADsContainer(iface)->IADsGroup_iface, riid, obj);
}

static ULONG WINAPI container_AddRef(IADsContainer *iface)
{
    return IADsGroup_AddRef(&impl_from_IADsContainer(iface)->IADsGroup_iface);
}

static ULONG WINAPI container_Release(IADsContainer *iface)
{
    return IADsGroup_Release(&impl_from_IADsContainer(iface)->IADsGroup_iface);
}

static HRESULT WINAPI container_GetTypeInfoCount(IADsContainer *iface, UINT *count)
{
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI container_GetTypeInfo(IADsContainer *iface, UINT index, LCID lcid, ITypeInfo **info)
{
    if (index)
        return DISP_E_BADINDEX;
    return get_type_info(&IID_IADsContainer, info);
}

static HRESULT WINAPI container_GetIDsOfNames(IADsContainer *iface, REFIID riid, LPOLESTR *names, UINT count,
                                              LCID lcid, DISPID *ids)
{
    ITypeInfo *info;
    HRESULT hr;

    hr = container_GetTypeInfo(iface, 0, lcid, &info);
    if (FAILED(hr))
        return hr;
    hr = ITypeInfo_GetIDsOfNames(info, names, count, ids);
    ITypeInfo_Release(info);
    return hr;
}

static HRESULT WINAPI container_Invoke(IADsContainer *iface, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                       DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg)
{
    ITypeInfo *info;
    HRESULT hr;

    hr = container_GetTypeInfo(iface, 0, lcid, &info);
    if (FAILED(hr))
        return hr;
    hr = ITypeInfo_Invoke(info, iface, id, flags, params, result, excep, arg);
    ITypeInfo_Release(info);
    return hr;
}

static HRESULT WINAPI container_get_Count(IADsContainer *iface, LONG *count)
{
    /* TODO: enumerate the computer's users and groups */
    FIXME("%p\n", count);
    return E_NOTIMPL;
}

static HRESULT WINAPI container_get__NewEnum(IADsContainer *iface, IUnknown **enumerator)
{
    /* TODO: enumerate the computer's users and groups */
    FIXME("%p\n", enumerator);
    return E_NOTIMPL;
}

static HRESULT WINAPI container_get_Filter(IADsContainer *iface, VARIANT *filter)
{
    /* TODO: the class filter used by enumeration */
    FIXME("%p\n", filter);
    return E_NOTIMPL;
}

static HRESULT WINAPI container_put_Filter(IADsContainer *iface, VARIANT filter)
{
    /* TODO: the class filter used by enumeration */
    FIXME("%s\n", debugstr_variant(&filter));
    return E_NOTIMPL;
}

static HRESULT WINAPI container_get_Hints(IADsContainer *iface, VARIANT *hints)
{
    FIXME("%p\n", hints);
    return E_NOTIMPL;
}

static HRESULT WINAPI container_put_Hints(IADsContainer *iface, VARIANT hints)
{
    FIXME("%s\n", debugstr_variant(&hints));
    return E_NOTIMPL;
}

static HRESULT WINAPI container_GetObject(IADsContainer *iface, BSTR cls, BSTR relative, IDispatch **disp)
{
    ADS_OBJECT *object = impl_from_IADsContainer(iface);
    WCHAR path[ARRAYSIZE(WinNTPrefix) + MAX_COMPUTERNAME_LENGTH + UNLEN + 64];
    HRESULT hr;

    TRACE("%s %s %p\n", debugstr_w(cls), debugstr_w(relative), disp);

    if (!disp)
        return E_POINTER;
    *disp = NULL;
    if (!relative || !relative[0])
        return E_ADS_BAD_PATHNAME;
    if (cls && cls[0])
        hr = StringCchPrintfW(path, ARRAYSIZE(path), L"%s%s/%s,%s", WinNTPrefix, object->name, relative, cls);
    else
        hr = StringCchPrintfW(path, ARRAYSIZE(path), L"%s%s/%s", WinNTPrefix, object->name, relative);
    if (FAILED(hr))
        return E_ADS_BAD_PATHNAME;
    return open_object(path, disp);
}

static HRESULT WINAPI container_Create(IADsContainer *iface, BSTR cls, BSTR relative, IDispatch **disp)
{
    /* TODO: create a local user or group */
    FIXME("%s %s %p\n", debugstr_w(cls), debugstr_w(relative), disp);
    return E_NOTIMPL;
}

static HRESULT WINAPI container_Delete(IADsContainer *iface, BSTR cls, BSTR relative)
{
    /* TODO: delete a local user or group */
    FIXME("%s %s\n", debugstr_w(cls), debugstr_w(relative));
    return E_NOTIMPL;
}

static HRESULT WINAPI container_CopyHere(IADsContainer *iface, BSTR source, BSTR name, IDispatch **disp)
{
    FIXME("%s %s %p\n", debugstr_w(source), debugstr_w(name), disp);
    return E_NOTIMPL;
}

static HRESULT WINAPI container_MoveHere(IADsContainer *iface, BSTR source, BSTR name, IDispatch **disp)
{
    /* TODO: rename a local user or group */
    FIXME("%s %s %p\n", debugstr_w(source), debugstr_w(name), disp);
    return E_NOTIMPL;
}

static const IADsContainerVtbl ContainerVtbl =
{
    container_QueryInterface,
    container_AddRef,
    container_Release,
    container_GetTypeInfoCount,
    container_GetTypeInfo,
    container_GetIDsOfNames,
    container_Invoke,
    container_get_Count,
    container_get__NewEnum,
    container_get_Filter,
    container_put_Filter,
    container_get_Hints,
    container_put_Hints,
    container_GetObject,
    container_Create,
    container_Delete,
    container_CopyHere,
    container_MoveHere
};

static HRESULT find_account(PCWSTR name, PCWSTR cls, OBJECT_TYPE *type)
{
    BOOL want_group = !lstrcmpiW(cls, L"group"), want_user = !lstrcmpiW(cls, L"user");
    LOCALGROUP_INFO_0 *group = NULL;
    USER_INFO_0 *user = NULL;
    NET_API_STATUS status;

    *type = ObjectGroup;
    /* TODO: services, printers and the other WinNT classes */
    if (cls[0] && !want_group && !want_user)
        return E_ADS_UNKNOWN_OBJECT;

    if (!want_user)
    {
        status = NetLocalGroupGetInfo(NULL, name, 0, (LPBYTE *)&group);
        if (status == NERR_Success)
        {
            NetApiBufferFree(group);
            *type = ObjectGroup;
            return S_OK;
        }
        if (want_group)
            return status_to_hresult(status);
    }

    status = NetUserGetInfo(NULL, name, 0, (LPBYTE *)&user);
    if (status != NERR_Success)
        return status_to_hresult(status);
    NetApiBufferFree(user);
    *type = ObjectUser;
    return S_OK;
}

/* A path without a class names the local group or the user of that name, or the computer itself */
static HRESULT open_object(PCWSTR path, IDispatch **disp)
{
    WCHAR container[MAX_COMPUTERNAME_LENGTH + 1], name[UNLEN + 1], cls[32];
    OBJECT_TYPE type;
    ADS_OBJECT *object;
    HRESULT hr;

    *disp = NULL;
    hr = parse_path(path, container, name, cls, ARRAYSIZE(cls));
    if (FAILED(hr))
        return hr;

    if (!name[0])
    {
        if (cls[0] && lstrcmpiW(cls, L"computer"))
            return E_ADS_BAD_PATHNAME;
        type = ObjectComputer;
    }
    else
    {
        hr = find_account(name, cls, &type);
        if (FAILED(hr))
            return hr;
    }

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    object->IADsGroup_iface.lpVtbl = &ObjectVtbl;
    object->IADsContainer_iface.lpVtbl = &ContainerVtbl;
    object->ref = 1;
    object->type = type;
    lstrcpyW(object->container, container);
    lstrcpyW(object->name, type == ObjectComputer ? container : name);

    *disp = (IDispatch *)&object->IADsGroup_iface;
    return S_OK;
}

static inline NAMESPACE *impl_from_IADsOpenDSObject(IADsOpenDSObject *iface)
{
    return CONTAINING_RECORD(iface, NAMESPACE, IADsOpenDSObject_iface);
}

static HRESULT WINAPI namespace_QueryInterface(IADsOpenDSObject *iface, REFIID riid, void **obj)
{
    *obj = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDispatch) ||
        IsEqualGUID(riid, &IID_IADsOpenDSObject))
    {
        *obj = iface;
        IADsOpenDSObject_AddRef(iface);
        return S_OK;
    }

    /* TODO: IADs, IADsContainer and IParseDisplayName on the namespace */
    FIXME("unsupported interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI namespace_AddRef(IADsOpenDSObject *iface)
{
    return InterlockedIncrement(&impl_from_IADsOpenDSObject(iface)->ref);
}

static ULONG WINAPI namespace_Release(IADsOpenDSObject *iface)
{
    NAMESPACE *ns = impl_from_IADsOpenDSObject(iface);
    LONG ref = InterlockedDecrement(&ns->ref);

    if (!ref)
        HeapFree(GetProcessHeap(), 0, ns);
    return ref;
}

static HRESULT WINAPI namespace_GetTypeInfoCount(IADsOpenDSObject *iface, UINT *count)
{
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI namespace_GetTypeInfo(IADsOpenDSObject *iface, UINT index, LCID lcid, ITypeInfo **info)
{
    if (index)
        return DISP_E_BADINDEX;
    return get_type_info(&IID_IADsOpenDSObject, info);
}

static HRESULT WINAPI namespace_GetIDsOfNames(IADsOpenDSObject *iface, REFIID riid, LPOLESTR *names, UINT count,
                                              LCID lcid, DISPID *ids)
{
    ITypeInfo *info;
    HRESULT hr;

    hr = namespace_GetTypeInfo(iface, 0, lcid, &info);
    if (FAILED(hr))
        return hr;
    hr = ITypeInfo_GetIDsOfNames(info, names, count, ids);
    ITypeInfo_Release(info);
    return hr;
}

static HRESULT WINAPI namespace_Invoke(IADsOpenDSObject *iface, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                       DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg)
{
    ITypeInfo *info;
    HRESULT hr;

    hr = namespace_GetTypeInfo(iface, 0, lcid, &info);
    if (FAILED(hr))
        return hr;
    hr = ITypeInfo_Invoke(info, iface, id, flags, params, result, excep, arg);
    ITypeInfo_Release(info);
    return hr;
}

static HRESULT WINAPI namespace_OpenDSObject(IADsOpenDSObject *iface, BSTR path, BSTR user, BSTR password,
                                             LONG reserved, IDispatch **obj)
{
    TRACE("%s %s %#lx\n", debugstr_w(path), debugstr_w(user), reserved);

    /* TODO: credentials for another computer or a domain */
    if (!obj)
        return E_POINTER;
    return open_object(path, obj);
}

static const IADsOpenDSObjectVtbl NamespaceVtbl =
{
    namespace_QueryInterface,
    namespace_AddRef,
    namespace_Release,
    namespace_GetTypeInfoCount,
    namespace_GetTypeInfo,
    namespace_GetIDsOfNames,
    namespace_Invoke,
    namespace_OpenDSObject
};

static inline PROVIDER *impl_from_IParseDisplayName(IParseDisplayName *iface)
{
    return CONTAINING_RECORD(iface, PROVIDER, IParseDisplayName_iface);
}

static HRESULT WINAPI provider_QueryInterface(IParseDisplayName *iface, REFIID riid, void **obj)
{
    *obj = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IParseDisplayName))
    {
        *obj = iface;
        IParseDisplayName_AddRef(iface);
        return S_OK;
    }

    /* TODO: IADs and IADsContainer on the provider object */
    FIXME("unsupported interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI provider_AddRef(IParseDisplayName *iface)
{
    return InterlockedIncrement(&impl_from_IParseDisplayName(iface)->ref);
}

static ULONG WINAPI provider_Release(IParseDisplayName *iface)
{
    PROVIDER *provider = impl_from_IParseDisplayName(iface);
    LONG ref = InterlockedDecrement(&provider->ref);

    if (!ref)
        HeapFree(GetProcessHeap(), 0, provider);
    return ref;
}

static HRESULT WINAPI provider_ParseDisplayName(IParseDisplayName *iface, IBindCtx *ctx, LPOLESTR name, ULONG *eaten,
                                                IMoniker **moniker)
{
    IDispatch *disp;
    HRESULT hr;

    TRACE("%p %s %p %p\n", ctx, debugstr_w(name), eaten, moniker);

    if (!name || !eaten || !moniker)
        return E_POINTER;
    *eaten = 0;
    *moniker = NULL;
    hr = open_object(name, &disp);
    if (FAILED(hr))
        return hr;
    hr = CreatePointerMoniker((IUnknown *)disp, moniker);
    IDispatch_Release(disp);
    if (SUCCEEDED(hr))
        *eaten = lstrlenW(name);
    return hr;
}

static const IParseDisplayNameVtbl ProviderVtbl =
{
    provider_QueryInterface,
    provider_AddRef,
    provider_Release,
    provider_ParseDisplayName
};

static HRESULT WINAPI factory_QueryInterface(IClassFactory *iface, REFIID riid, void **obj)
{
    *obj = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IClassFactory))
    {
        *obj = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI factory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **obj)
{
    NAMESPACE *ns;
    HRESULT hr;

    *obj = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;

    ns = HeapAlloc(GetProcessHeap(), 0, sizeof(*ns));
    if (!ns)
        return E_OUTOFMEMORY;
    ns->IADsOpenDSObject_iface.lpVtbl = &NamespaceVtbl;
    ns->ref = 1;

    hr = IADsOpenDSObject_QueryInterface(&ns->IADsOpenDSObject_iface, riid, obj);
    IADsOpenDSObject_Release(&ns->IADsOpenDSObject_iface);
    return hr;
}

static HRESULT WINAPI factory_LockServer(IClassFactory *iface, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl FactoryVtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_CreateInstance,
    factory_LockServer
};

static IClassFactory NamespaceFactory = { &FactoryVtbl };

static HRESULT WINAPI provider_factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **obj)
{
    PROVIDER *provider;
    HRESULT hr;

    *obj = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;

    provider = HeapAlloc(GetProcessHeap(), 0, sizeof(*provider));
    if (!provider)
        return E_OUTOFMEMORY;
    provider->IParseDisplayName_iface.lpVtbl = &ProviderVtbl;
    provider->ref = 1;

    hr = IParseDisplayName_QueryInterface(&provider->IParseDisplayName_iface, riid, obj);
    IParseDisplayName_Release(&provider->IParseDisplayName_iface);
    return hr;
}

static const IClassFactoryVtbl ProviderFactoryVtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    provider_factory_CreateInstance,
    factory_LockServer
};

static IClassFactory ProviderFactory = { &ProviderFactoryVtbl };

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **obj)
{
    TRACE("%s %s %p\n", debugstr_guid(clsid), debugstr_guid(riid), obj);

    if (IsEqualGUID(clsid, &CLSID_WinNTNamespaceObject))
        return IClassFactory_QueryInterface(&NamespaceFactory, riid, obj);
    if (IsEqualGUID(clsid, &CLSID_WinNTProviderObject))
        return IClassFactory_QueryInterface(&ProviderFactory, riid, obj);

    *obj = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}
