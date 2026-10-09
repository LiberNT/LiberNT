/*
 * PROJECT:     LiberNT shdocvw
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shell folder for documents that are browsed in place
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "objects.h"

STDMETHODIMP
CShellDocObjView::ParseDisplayName(HWND hwndOwner, LPBC pbc, LPOLESTR pszDisplayName, ULONG *pchEaten,
                                   PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes)
{
    return E_NOTIMPL;
}

STDMETHODIMP
CShellDocObjView::EnumObjects(HWND hwndOwner, SHCONTF grfFlags, IEnumIDList **ppenumIDList)
{
    return E_UNEXPECTED;
}

STDMETHODIMP
CShellDocObjView::BindToObject(PCUIDLIST_RELATIVE pidl, LPBC pbc, REFIID riid, void **ppv)
{
    return E_NOTIMPL;
}

STDMETHODIMP
CShellDocObjView::BindToStorage(PCUIDLIST_RELATIVE pidl, LPBC pbc, REFIID riid, void **ppv)
{
    return E_NOTIMPL;
}

STDMETHODIMP
CShellDocObjView::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    return E_NOTIMPL;
}

STDMETHODIMP
CShellDocObjView::CreateViewObject(HWND hwndOwner, REFIID riid, void **ppv)
{
    return E_NOTIMPL;
}

STDMETHODIMP
CShellDocObjView::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF *rgfInOut)
{
    return E_NOTIMPL;
}

STDMETHODIMP
CShellDocObjView::GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid,
                                UINT *rgfReserved, void **ppv)
{
    return E_NOTIMPL;
}

STDMETHODIMP
CShellDocObjView::GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET *pName)
{
    return E_NOTIMPL;
}

STDMETHODIMP
CShellDocObjView::SetNameOf(HWND hwnd, PCUITEMID_CHILD pidl, LPCOLESTR pszName, SHGDNF uFlags,
                            PITEMID_CHILD *ppidlOut)
{
    return E_NOTIMPL;
}

STDMETHODIMP
CShellDocObjView::GetClassID(CLSID *pClassID)
{
    if (!pClassID)
        return E_POINTER;
    *pClassID = CLSID_ShellDocObjView;
    return S_OK;
}

STDMETHODIMP
CShellDocObjView::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    PIDLIST_ABSOLUTE pidlCopy;

    if (!pidl)
        return E_INVALIDARG;
    pidlCopy = ILClone(pidl);
    if (!pidlCopy)
        return E_OUTOFMEMORY;
    m_pidl.Free();
    m_pidl.Attach(pidlCopy);
    return S_OK;
}

STDMETHODIMP
CShellDocObjView::GetCurFolder(PIDLIST_ABSOLUTE *ppidl)
{
    if (!ppidl)
        return E_POINTER;
    *ppidl = NULL;
    if (!m_pidl)
        return S_FALSE;
    *ppidl = ILClone(m_pidl);
    return *ppidl ? S_OK : E_OUTOFMEMORY;
}
