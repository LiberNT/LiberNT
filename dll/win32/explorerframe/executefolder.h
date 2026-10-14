/*
 * PROJECT:     LiberNT Explorer Frame
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Folder verb handler that opens a folder window through the shell
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

class CExecuteFolder :
    public CComCoClass<CExecuteFolder, &CLSID_ExecuteFolder>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IExecuteCommand,
    public IObjectWithSelection,
    public IInitializeCommand
{
    CComPtr<IShellItemArray> m_Selection;
    int m_Show;
    BOOL m_Explore;

public:
    CExecuteFolder();

    STDMETHOD(SetKeyState)(DWORD grfKeyState) override;
    STDMETHOD(SetParameters)(LPCWSTR pszParameters) override;
    STDMETHOD(SetPosition)(POINT pt) override;
    STDMETHOD(SetShowWindow)(int nShow) override;
    STDMETHOD(SetNoShowUI)(BOOL fNoShowUI) override;
    STDMETHOD(SetDirectory)(LPCWSTR pszDirectory) override;
    STDMETHOD(Execute)() override;

    STDMETHOD(SetSelection)(IShellItemArray *psia) override;
    STDMETHOD(GetSelection)(REFIID riid, void **ppv) override;

    STDMETHOD(Initialize)(LPCWSTR pszCommandName, IPropertyBag *ppb) override;

    DECLARE_REGISTRY_RESOURCEID(IDR_EXECUTEFOLDER)
    DECLARE_NOT_AGGREGATABLE(CExecuteFolder)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CExecuteFolder)
        COM_INTERFACE_ENTRY_IID(IID_IExecuteCommand, IExecuteCommand)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSelection, IObjectWithSelection)
        COM_INTERFACE_ENTRY_IID(IID_IInitializeCommand, IInitializeCommand)
    END_COM_MAP()
};
