/*
 * PROJECT:     LiberNT Windows Storage
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     IShellTaskScheduler implementation
 * COPYRIGHT:   Copyright 2020 Oleg Dubinskiy (oleg.dubinskij30@gmail.com)
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "objects.h"
#include "CShellTaskScheduler.h"

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(storage);

#define SCHEDULER_MAX_THREADS 16

struct TASKITEM
{
    TASKITEM *pNext;
    IRunnableTask *pTask;
    GUID toid;
    DWORD_PTR lParam;
    DWORD dwPriority;
    DWORD dwThreadId;
    BOOL bSuspended;
};

class CTaskSchedulerCore
{
    LONG m_cRefs;
    CRITICAL_SECTION m_cs;
    TASKITEM *m_pQueue;
    TASKITEM *m_pRunning;
    HANDLE m_hWorkEvent;
    HANDLE m_hDoneEvent;
    HANDLE m_hThreads[SCHEDULER_MAX_THREADS];
    DWORD m_dwThreadIds[SCHEDULER_MAX_THREADS];
    INT m_cIdle;

    static DWORD WINAPI WorkerProc(LPVOID pvParam);

    static BOOL Matches(const TASKITEM *pItem, REFGUID rtoid, DWORD_PTR lParam)
    {
        if (!IsEqualGUID(rtoid, TOID_NULL) && !IsEqualGUID(rtoid, pItem->toid))
            return FALSE;
        return lParam == ITSAT_DEFAULT_LPARAM || lParam == pItem->lParam;
    }

    static void ReleaseItem(TASKITEM *pItem)
    {
        pItem->pTask->Release();
        pItem->pTask->Release();
        delete pItem;
    }

    void InsertLocked(TASKITEM *pItem)
    {
        TASKITEM **ppNext = &m_pQueue;

        if (pItem->bSuspended)
        {
            while (*ppNext && (*ppNext)->dwPriority > pItem->dwPriority)
                ppNext = &(*ppNext)->pNext;
        }
        else
        {
            while (*ppNext && (*ppNext)->dwPriority >= pItem->dwPriority)
                ppNext = &(*ppNext)->pNext;
        }
        pItem->pNext = *ppNext;
        *ppNext = pItem;
        SetEvent(m_hWorkEvent);
    }

    BOOL StartWorkerLocked()
    {
        INT cActive = 0, iFree = -1;

        if (m_cIdle > 0)
            return TRUE;

        for (INT i = 0; i < SCHEDULER_MAX_THREADS; ++i)
        {
            if (m_hThreads[i] && WaitForSingleObject(m_hThreads[i], 0) == WAIT_OBJECT_0)
            {
                CloseHandle(m_hThreads[i]);
                m_hThreads[i] = NULL;
                m_dwThreadIds[i] = 0;
            }
            if (m_hThreads[i])
                ++cActive;
            else if (iFree < 0)
                iFree = i;
        }

        if (cActive >= min(m_cThreadsMax, SCHEDULER_MAX_THREADS) || iFree < 0)
            return FALSE;

        AddRef();
        HANDLE hThread = CreateThread(NULL, 0, WorkerProc, this, CREATE_SUSPENDED, &m_dwThreadIds[iFree]);
        if (!hThread)
        {
            InterlockedDecrement(&m_cRefs);
            return FALSE;
        }
        SetThreadPriority(hThread, m_nThreadPriority);
        m_hThreads[iFree] = hThread;
        ResumeThread(hThread);
        return TRUE;
    }

    TASKITEM *TakeNext()
    {
        Lock();
        for (;;)
        {
            if (m_bShutdown)
                break;

            if (m_pQueue)
            {
                TASKITEM *pItem = m_pQueue;
                m_pQueue = pItem->pNext;
                pItem->pNext = m_pRunning;
                pItem->dwThreadId = GetCurrentThreadId();
                m_pRunning = pItem;
                Unlock();
                return pItem;
            }

            ResetEvent(m_hWorkEvent);
            ++m_cIdle;
            DWORD dwTimeout = m_dwThreadTimeout;
            Unlock();
            DWORD dwWait = WaitForSingleObject(m_hWorkEvent, dwTimeout);
            Lock();
            --m_cIdle;
            if (dwWait == WAIT_TIMEOUT && !m_pQueue)
                break;
        }
        Unlock();
        return NULL;
    }

    void TaskDone(TASKITEM *pItem, HRESULT hrRun)
    {
        Lock();
        for (TASKITEM **ppNext = &m_pRunning; *ppNext; ppNext = &(*ppNext)->pNext)
        {
            if (*ppNext == pItem)
            {
                *ppNext = pItem->pNext;
                break;
            }
        }
        pItem->pNext = NULL;
        pItem->dwThreadId = 0;
        SetEvent(m_hDoneEvent);

        if (hrRun == E_PENDING && !m_bShutdown)
        {
            pItem->bSuspended = TRUE;
            InsertLocked(pItem);
            Unlock();
            return;
        }
        Unlock();
        ReleaseItem(pItem);
    }

public:
    INT m_cThreadsMax;
    INT m_nThreadPriority;
    DWORD m_dwReleaseStatus;
    DWORD m_dwThreadTimeout;
    DWORD m_dwFlags;
    BOOL m_bShutdown;

    CTaskSchedulerCore()
        : m_cRefs(1), m_pQueue(NULL), m_pRunning(NULL), m_hWorkEvent(NULL), m_hDoneEvent(NULL), m_cIdle(0),
          m_cThreadsMax(1), m_nThreadPriority(THREAD_PRIORITY_NORMAL),
          m_dwReleaseStatus(ITSSFLAG_COMPLETE_ON_DESTROY), m_dwThreadTimeout(ITSS_THREAD_DESTROY_DEFAULT_TIMEOUT),
          m_dwFlags(0), m_bShutdown(FALSE)
    {
        InitializeCriticalSection(&m_cs);
        ZeroMemory(m_hThreads, sizeof(m_hThreads));
        ZeroMemory(m_dwThreadIds, sizeof(m_dwThreadIds));
    }

    ~CTaskSchedulerCore()
    {
        for (INT i = 0; i < SCHEDULER_MAX_THREADS; ++i)
        {
            if (m_hThreads[i])
                CloseHandle(m_hThreads[i]);
        }
        if (m_hWorkEvent)
            CloseHandle(m_hWorkEvent);
        if (m_hDoneEvent)
            CloseHandle(m_hDoneEvent);
        DeleteCriticalSection(&m_cs);
    }

    BOOL Init()
    {
        m_hWorkEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        m_hDoneEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        return m_hWorkEvent && m_hDoneEvent;
    }

    void AddRef()
    {
        InterlockedIncrement(&m_cRefs);
    }

    void Release()
    {
        if (!InterlockedDecrement(&m_cRefs))
            delete this;
    }

    void Lock()
    {
        EnterCriticalSection(&m_cs);
    }

    void Unlock()
    {
        LeaveCriticalSection(&m_cs);
    }

    HRESULT Add(IRunnableTask *pTask, REFGUID rtoid, DWORD_PTR lParam, DWORD dwPriority)
    {
        IRunnableTask *pSuspend = NULL;
        DWORD dwLowest = dwPriority;
        TASKITEM *pItem = new TASKITEM;

        if (!pItem)
            return E_OUTOFMEMORY;

        pTask->AddRef();
        pTask->AddRef();
        pItem->pNext = NULL;
        pItem->pTask = pTask;
        pItem->toid = rtoid;
        pItem->lParam = lParam;
        pItem->dwPriority = dwPriority;
        pItem->dwThreadId = 0;
        pItem->bSuspended = FALSE;

        Lock();
        InsertLocked(pItem);
        if (!StartWorkerLocked())
        {
            for (TASKITEM *pRun = m_pRunning; pRun; pRun = pRun->pNext)
            {
                if (pRun->dwPriority < dwLowest)
                {
                    dwLowest = pRun->dwPriority;
                    pSuspend = pRun->pTask;
                }
            }
            if (pSuspend)
                pSuspend->AddRef();
        }
        Unlock();

        if (pSuspend)
        {
            pSuspend->Suspend();
            pSuspend->Release();
        }
        return S_OK;
    }

    void Remove(REFGUID rtoid, DWORD_PTR lParam, BOOL fWaitIfRunning)
    {
        TASKITEM *pRemoved = NULL;
        IRunnableTask *pKill[SCHEDULER_MAX_THREADS];
        INT cKill = 0;
        DWORD dwSelf = GetCurrentThreadId();

        Lock();
        for (TASKITEM **ppNext = &m_pQueue; *ppNext;)
        {
            TASKITEM *pItem = *ppNext;
            if (Matches(pItem, rtoid, lParam))
            {
                *ppNext = pItem->pNext;
                pItem->pNext = pRemoved;
                pRemoved = pItem;
            }
            else
            {
                ppNext = &pItem->pNext;
            }
        }
        if (m_dwReleaseStatus & ITSSFLAG_KILL_ON_DESTROY)
        {
            for (TASKITEM *pRun = m_pRunning; pRun && cKill < SCHEDULER_MAX_THREADS; pRun = pRun->pNext)
            {
                if (Matches(pRun, rtoid, lParam))
                {
                    pRun->pTask->AddRef();
                    pKill[cKill++] = pRun->pTask;
                }
            }
        }
        Unlock();

        while (pRemoved)
        {
            TASKITEM *pNext = pRemoved->pNext;
            ReleaseItem(pRemoved);
            pRemoved = pNext;
        }

        for (INT i = 0; i < cKill; ++i)
        {
            pKill[i]->Kill(fWaitIfRunning);
            pKill[i]->Release();
        }

        if (!fWaitIfRunning)
            return;

        Lock();
        for (;;)
        {
            BOOL bRunning = FALSE;
            for (TASKITEM *pRun = m_pRunning; pRun; pRun = pRun->pNext)
            {
                if (pRun->dwThreadId != dwSelf && Matches(pRun, rtoid, lParam))
                    bRunning = TRUE;
            }
            if (!bRunning)
                break;
            ResetEvent(m_hDoneEvent);
            Unlock();
            WaitForSingleObject(m_hDoneEvent, INFINITE);
            Lock();
        }
        Unlock();
    }

    UINT Count(REFGUID rtoid)
    {
        UINT cTasks = 0;

        Lock();
        for (TASKITEM *pItem = m_pQueue; pItem; pItem = pItem->pNext)
        {
            if (Matches(pItem, rtoid, ITSAT_DEFAULT_LPARAM))
                ++cTasks;
        }
        for (TASKITEM *pItem = m_pRunning; pItem; pItem = pItem->pNext)
        {
            if (Matches(pItem, rtoid, ITSAT_DEFAULT_LPARAM))
                ++cTasks;
        }
        Unlock();
        return cTasks;
    }

    void Move(REFGUID rtoid, DWORD_PTR lParam, DWORD dwPriority)
    {
        TASKITEM *pMoved = NULL;

        Lock();
        for (TASKITEM **ppNext = &m_pQueue; *ppNext;)
        {
            TASKITEM *pItem = *ppNext;
            if (Matches(pItem, rtoid, lParam))
            {
                *ppNext = pItem->pNext;
                pItem->pNext = pMoved;
                pMoved = pItem;
            }
            else
            {
                ppNext = &pItem->pNext;
            }
        }
        while (pMoved)
        {
            TASKITEM *pNext = pMoved->pNext;
            pMoved->dwPriority = dwPriority;
            InsertLocked(pMoved);
            pMoved = pNext;
        }
        Unlock();
    }

    void SetPriority(INT nPriority)
    {
        Lock();
        m_nThreadPriority = nPriority;
        for (INT i = 0; i < SCHEDULER_MAX_THREADS; ++i)
        {
            if (m_hThreads[i])
                SetThreadPriority(m_hThreads[i], nPriority);
        }
        Unlock();
    }

    void Shutdown()
    {
        TASKITEM *pQueue;
        IRunnableTask *pKill[SCHEDULER_MAX_THREADS];
        HANDLE hWait[SCHEDULER_MAX_THREADS];
        INT cKill = 0, cWait = 0;
        DWORD dwSelf = GetCurrentThreadId();

        Lock();
        m_bShutdown = TRUE;
        pQueue = m_pQueue;
        m_pQueue = NULL;
        if (m_dwReleaseStatus & ITSSFLAG_KILL_ON_DESTROY)
        {
            for (TASKITEM *pRun = m_pRunning; pRun && cKill < SCHEDULER_MAX_THREADS; pRun = pRun->pNext)
            {
                pRun->pTask->AddRef();
                pKill[cKill++] = pRun->pTask;
            }
        }
        for (INT i = 0; i < SCHEDULER_MAX_THREADS; ++i)
        {
            if (m_hThreads[i] && m_dwThreadIds[i] != dwSelf)
                hWait[cWait++] = m_hThreads[i];
        }
        SetEvent(m_hWorkEvent);
        Unlock();

        while (pQueue)
        {
            TASKITEM *pNext = pQueue->pNext;
            ReleaseItem(pQueue);
            pQueue = pNext;
        }

        for (INT i = 0; i < cKill; ++i)
        {
            pKill[i]->Kill(FALSE);
            pKill[i]->Release();
        }

        if (cWait)
            WaitForMultipleObjects(cWait, hWait, TRUE, INFINITE);
    }

    void WorkerLoop()
    {
        for (;;)
        {
            TASKITEM *pItem = TakeNext();
            if (!pItem)
                break;

            HRESULT hr = pItem->bSuspended ? pItem->pTask->Resume() : pItem->pTask->Run();
            TaskDone(pItem, hr);
        }
    }
};

DWORD WINAPI CTaskSchedulerCore::WorkerProc(LPVOID pvParam)
{
    CTaskSchedulerCore *pCore = (CTaskSchedulerCore *)pvParam;
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    pCore->WorkerLoop();

    if (SUCCEEDED(hrInit))
        CoUninitialize();
    pCore->Release();
    return 0;
}

CShellTaskScheduler::CShellTaskScheduler()
    : m_pCore(NULL)
{
}

CShellTaskScheduler::~CShellTaskScheduler()
{
}

HRESULT CShellTaskScheduler::FinalConstruct()
{
    m_pCore = new CTaskSchedulerCore();
    if (!m_pCore)
        return E_OUTOFMEMORY;
    if (!m_pCore->Init())
        return HRESULT_FROM_WIN32(GetLastError());
    return CoCreateFreeThreadedMarshaler(static_cast<IShellTaskScheduler2 *>(this), &m_pUnkMarshaler);
}

void CShellTaskScheduler::FinalRelease()
{
    if (m_pCore)
    {
        m_pCore->Shutdown();
        m_pCore->Release();
        m_pCore = NULL;
    }
    m_pUnkMarshaler.Release();
}

// *** IShellTaskScheduler methods ***
STDMETHODIMP CShellTaskScheduler::AddTask(IRunnableTask *pTask, REFGUID rtoid, DWORD_PTR lParam, DWORD dwPriority)
{
    TRACE("(%p, %p, %s, %Ix, %lx)\n", this, pTask, debugstr_guid(&rtoid), lParam, dwPriority);

    if (!pTask)
        return E_INVALIDARG;
    return m_pCore->Add(pTask, rtoid, lParam, dwPriority);
}

STDMETHODIMP CShellTaskScheduler::RemoveTasks(REFGUID rtoid, DWORD_PTR lParam, BOOL fWaitIfRunning)
{
    TRACE("(%p, %s, %Ix, %d)\n", this, debugstr_guid(&rtoid), lParam, fWaitIfRunning);

    m_pCore->Remove(rtoid, lParam, fWaitIfRunning);
    return S_OK;
}

UINT STDMETHODCALLTYPE CShellTaskScheduler::CountTasks(REFGUID rtoid)
{
    TRACE("(%p, %s)\n", this, debugstr_guid(&rtoid));

    return m_pCore->Count(rtoid);
}

STDMETHODIMP CShellTaskScheduler::Status(DWORD dwReleaseStatus, DWORD dwThreadTimeout)
{
    TRACE("(%p, %lx, %lu)\n", this, dwReleaseStatus, dwThreadTimeout);

    m_pCore->Lock();
    m_pCore->m_dwReleaseStatus = dwReleaseStatus & ITSSFLAG_FLAGS_MASK;
    if (dwThreadTimeout != ITSS_THREAD_TIMEOUT_NO_CHANGE)
        m_pCore->m_dwThreadTimeout = dwThreadTimeout;
    m_pCore->Unlock();
    return S_OK;
}

// *** IShellTaskScheduler2 methods ***
STDMETHODIMP CShellTaskScheduler::AddTask2(IRunnableTask *pTask, REFGUID rtoid, DWORD_PTR lParam, DWORD dwPriority, DWORD dwFlags)
{
    if (dwFlags)
        FIXME("(%p, %p, %s, %Ix, %lx, %lx): flags ignored\n", this, pTask, debugstr_guid(&rtoid), lParam, dwPriority, dwFlags);
    return AddTask(pTask, rtoid, lParam, dwPriority);
}

STDMETHODIMP CShellTaskScheduler::MoveTask(REFGUID rtoid, DWORD_PTR lParam, DWORD dwPriority, DWORD dwFlags)
{
    if (dwFlags)
        FIXME("(%p, %s, %Ix, %lx, %lx): flags ignored\n", this, debugstr_guid(&rtoid), lParam, dwPriority, dwFlags);
    m_pCore->Move(rtoid, lParam, dwPriority);
    return S_OK;
}

// *** IShellTaskSchedulerSettings methods ***
STDMETHODIMP CShellTaskScheduler::SetWorkerThreadCountMax(INT cThreads)
{
    if (cThreads <= 0)
        return E_INVALIDARG;
    m_pCore->Lock();
    m_pCore->m_cThreadsMax = cThreads;
    m_pCore->Unlock();
    return S_OK;
}

STDMETHODIMP CShellTaskScheduler::GetWorkerThreadCountMax(INT *pcThreads)
{
    if (!pcThreads)
        return E_POINTER;
    m_pCore->Lock();
    *pcThreads = m_pCore->m_cThreadsMax;
    m_pCore->Unlock();
    return S_OK;
}

STDMETHODIMP CShellTaskScheduler::SetWorkerThreadPriority(INT nPriority)
{
    m_pCore->SetPriority(nPriority);
    return S_OK;
}

STDMETHODIMP CShellTaskScheduler::SetFlags(DWORD dwMask, DWORD dwFlags)
{
    m_pCore->Lock();
    m_pCore->m_dwFlags = (m_pCore->m_dwFlags & ~dwMask) | (dwFlags & dwMask);
    m_pCore->Unlock();
    return S_OK;
}

STDMETHODIMP CShellTaskScheduler::GetFlags(DWORD *pdwFlags)
{
    if (!pdwFlags)
        return E_POINTER;
    m_pCore->Lock();
    *pdwFlags = m_pCore->m_dwFlags;
    m_pCore->Unlock();
    return S_OK;
}

OBJECT_ENTRY_AUTO(CLSID_ShellTaskScheduler, CShellTaskScheduler)
