/*
 * PROJECT:     LiberNT Task Scheduler
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Instance of a task started on demand
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#define COBJMACROS

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "taskschd.h"
#include "schrpc.h"
#include "taskschd_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(taskschd);

typedef struct
{
    IRunningTask IRunningTask_iface;
    LONG ref;
    WCHAR *path;
    GUID guid;
} RunningTask;

static inline RunningTask *impl_from_IRunningTask(IRunningTask *iface)
{
    return CONTAINING_RECORD(iface, RunningTask, IRunningTask_iface);
}

static ULONG WINAPI runtask_AddRef(IRunningTask *iface)
{
    RunningTask *task = impl_from_IRunningTask(iface);
    return InterlockedIncrement(&task->ref);
}

static ULONG WINAPI runtask_Release(IRunningTask *iface)
{
    RunningTask *task = impl_from_IRunningTask(iface);
    LONG ref = InterlockedDecrement(&task->ref);

    if (!ref)
    {
        free(task->path);
        free(task);
    }
    return ref;
}

static HRESULT WINAPI runtask_QueryInterface(IRunningTask *iface, REFIID riid, void **obj)
{
    if (!riid || !obj) return E_INVALIDARG;
    if (IsEqualGUID(riid, &IID_IRunningTask) || IsEqualGUID(riid, &IID_IDispatch) || IsEqualGUID(riid, &IID_IUnknown))
    {
        IRunningTask_AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI runtask_GetTypeInfoCount(IRunningTask *iface, UINT *count)
{
    FIXME("%p,%p: stub\n", iface, count);
    return E_NOTIMPL;
}

static HRESULT WINAPI runtask_GetTypeInfo(IRunningTask *iface, UINT index, LCID lcid, ITypeInfo **info)
{
    FIXME("%p,%u,%lu,%p: stub\n", iface, index, lcid, info);
    return E_NOTIMPL;
}

static HRESULT WINAPI runtask_GetIDsOfNames(IRunningTask *iface, REFIID riid, LPOLESTR *names, UINT count,
                                            LCID lcid, DISPID *dispid)
{
    FIXME("%p,%s,%p,%u,%lu,%p: stub\n", iface, debugstr_guid(riid), names, count, lcid, dispid);
    return E_NOTIMPL;
}

static HRESULT WINAPI runtask_Invoke(IRunningTask *iface, DISPID dispid, REFIID riid, LCID lcid, WORD flags,
                                     DISPPARAMS *params, VARIANT *result, EXCEPINFO *excepinfo, UINT *argerr)
{
    FIXME("%p,%ld,%s,%04lx,%04x,%p,%p,%p,%p: stub\n", iface, dispid, debugstr_guid(riid), lcid, flags,
          params, result, excepinfo, argerr);
    return E_NOTIMPL;
}

static HRESULT WINAPI runtask_get_Name(IRunningTask *iface, BSTR *name)
{
    RunningTask *task = impl_from_IRunningTask(iface);
    const WCHAR *leaf;

    if (!name) return E_POINTER;
    leaf = wcsrchr(task->path, '\\');
    leaf = leaf && leaf[1] ? leaf + 1 : task->path;
    *name = SysAllocString(leaf);
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI runtask_get_InstanceGuid(IRunningTask *iface, BSTR *guid)
{
    RunningTask *task = impl_from_IRunningTask(iface);
    WCHAR text[39];

    if (!guid) return E_POINTER;
    StringFromGUID2(&task->guid, text, ARRAY_SIZE(text));
    *guid = SysAllocString(text);
    return *guid ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI runtask_get_Path(IRunningTask *iface, BSTR *path)
{
    RunningTask *task = impl_from_IRunningTask(iface);

    if (!path) return E_POINTER;
    *path = SysAllocString(task->path);
    return *path ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI runtask_get_State(IRunningTask *iface, TASK_STATE *state)
{
    if (!state) return E_POINTER;
    /* FIXME: the service does not track running instances; their state is reported unknown */
    *state = TASK_STATE_UNKNOWN;
    return S_OK;
}

static HRESULT WINAPI runtask_get_CurrentAction(IRunningTask *iface, BSTR *name)
{
    if (!name) return E_POINTER;
    /* TODO: report the action an instance is running */
    *name = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI runtask_Stop(IRunningTask *iface)
{
    RunningTask *task = impl_from_IRunningTask(iface);
    return SchRpcStopInstance(task->guid, 0);
}

static HRESULT WINAPI runtask_Refresh(IRunningTask *iface)
{
    return S_OK;
}

static HRESULT WINAPI runtask_get_EnginePID(IRunningTask *iface, DWORD *pid)
{
    if (!pid) return E_POINTER;
    /* FIXME: actions are started by the service itself, not by a task engine process */
    *pid = 0;
    return S_OK;
}

static const IRunningTaskVtbl RunningTask_vtbl =
{
    runtask_QueryInterface,
    runtask_AddRef,
    runtask_Release,
    runtask_GetTypeInfoCount,
    runtask_GetTypeInfo,
    runtask_GetIDsOfNames,
    runtask_Invoke,
    runtask_get_Name,
    runtask_get_InstanceGuid,
    runtask_get_Path,
    runtask_get_State,
    runtask_get_CurrentAction,
    runtask_Stop,
    runtask_Refresh,
    runtask_get_EnginePID
};

HRESULT RunningTask_create(const WCHAR *path, const GUID *guid, IRunningTask **out)
{
    RunningTask *task;
    SIZE_T size = (wcslen(path) + 1) * sizeof(WCHAR);

    *out = NULL;
    task = malloc(sizeof(*task));
    if (!task) return E_OUTOFMEMORY;
    task->path = malloc(size);
    if (!task->path)
    {
        free(task);
        return E_OUTOFMEMORY;
    }
    memcpy(task->path, path, size);
    task->IRunningTask_iface.lpVtbl = &RunningTask_vtbl;
    task->ref = 1;
    task->guid = *guid;
    *out = &task->IRunningTask_iface;
    return S_OK;
}
