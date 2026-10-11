/*
 * PROJECT:     LiberNT Windows Storage
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     IShellTaskScheduler implementation
 * COPYRIGHT:   Copyright 2020 Oleg Dubinskiy (oleg.dubinskij30@gmail.com)
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CTaskSchedulerCore;

class CShellTaskScheduler :
    public CComCoClass<CShellTaskScheduler, &CLSID_ShellTaskScheduler>,
    public CComObjectRootEx<CComMultiThreadModel>,
    public IShellTaskScheduler2,
    public IShellTaskSchedulerSettings
{
    CComPtr<IUnknown> m_pUnkMarshaler;
    CTaskSchedulerCore *m_pCore;

public:
    CShellTaskScheduler();
    ~CShellTaskScheduler();

    HRESULT FinalConstruct();
    void FinalRelease();

    // *** IShellTaskScheduler methods ***
    STDMETHODIMP AddTask(IRunnableTask *pTask, REFGUID rtoid, DWORD_PTR lParam, DWORD dwPriority) override;
    STDMETHODIMP RemoveTasks(REFGUID rtoid, DWORD_PTR lParam, BOOL fWaitIfRunning) override;
    STDMETHODIMP_(UINT) CountTasks(REFGUID rtoid) override;
    STDMETHODIMP Status(DWORD dwReleaseStatus, DWORD dwThreadTimeout) override;

    // *** IShellTaskScheduler2 methods ***
    STDMETHODIMP AddTask2(IRunnableTask *pTask, REFGUID rtoid, DWORD_PTR lParam, DWORD dwPriority, DWORD dwFlags) override;
    STDMETHODIMP MoveTask(REFGUID rtoid, DWORD_PTR lParam, DWORD dwPriority, DWORD dwFlags) override;

    // *** IShellTaskSchedulerSettings methods ***
    STDMETHODIMP SetWorkerThreadCountMax(INT cThreads) override;
    STDMETHODIMP GetWorkerThreadCountMax(INT *pcThreads) override;
    STDMETHODIMP SetWorkerThreadPriority(INT nPriority) override;
    STDMETHODIMP SetFlags(DWORD dwMask, DWORD dwFlags) override;
    STDMETHODIMP GetFlags(DWORD *pdwFlags) override;

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CShellTaskScheduler)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CShellTaskScheduler)
        COM_INTERFACE_ENTRY_IID(IID_IShellTaskScheduler, IShellTaskScheduler)
        COM_INTERFACE_ENTRY_IID(IID_IShellTaskScheduler2, IShellTaskScheduler2)
        COM_INTERFACE_ENTRY_IID(IID_IShellTaskSchedulerSettings, IShellTaskSchedulerSettings)
        COM_INTERFACE_ENTRY_AGGREGATE(IID_IMarshal, m_pUnkMarshaler.p)
        COM_INTERFACE_ENTRY_AGGREGATE(IID_IAgileObject, m_pUnkMarshaler.p)
    END_COM_MAP()
};
