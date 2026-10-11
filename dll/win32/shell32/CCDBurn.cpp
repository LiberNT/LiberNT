/*
 * PROJECT:     LiberNT shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     CD burning shell object (ICDBurn)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ntddcdrm.h>
#include <ntddmmc.h>

WINE_DEFAULT_DEBUG_CHANNEL(shell);

static BOOL CDBurn_HasWriteFeature(HANDLE hDevice, FEATURE_NUMBER Feature)
{
    GET_CONFIGURATION_IOCTL_INPUT Input = {};
    union
    {
        GET_CONFIGURATION_HEADER Header;
        BYTE Bytes[sizeof(GET_CONFIGURATION_HEADER) + 256];
    } Output = {};
    PFEATURE_HEADER pFeature = (PFEATURE_HEADER)Output.Header.Data;
    DWORD cbReturned = 0;

    Input.Feature = Feature;
    Input.RequestType = SCSI_GET_CONFIGURATION_REQUEST_TYPE_ONE;
    if (!DeviceIoControl(hDevice, IOCTL_CDROM_GET_CONFIGURATION, &Input, sizeof(Input),
                         &Output, sizeof(Output), &cbReturned, NULL))
        return FALSE;
    if (cbReturned < sizeof(GET_CONFIGURATION_HEADER) + sizeof(FEATURE_HEADER))
        return FALSE;
    if ((ULONG)((pFeature->FeatureCode[0] << 8) | pFeature->FeatureCode[1]) != (ULONG)Feature)
        return FALSE;

    if (Feature == FeatureDvdPlusRW || Feature == FeatureDvdPlusR)
    {
        if (cbReturned < sizeof(GET_CONFIGURATION_HEADER) + sizeof(FEATURE_HEADER) + 1 ||
            pFeature->AdditionalLength < 1)
            return FALSE;
        return ((PFEATURE_DATA_DVD_PLUS_R)pFeature)->Write;
    }
    return TRUE;
}

static BOOL CDBurn_IsRecorder(int iDrive)
{
    static const FEATURE_NUMBER WriteFeatures[] =
    {
        FeatureIncrementalStreamingWritable,
        FeatureRestrictedOverwrite,
        FeatureRigidRestrictedOverwrite,
        FeatureCdTrackAtOnce,
        FeatureCdMastering,
        FeatureDvdRecordableWrite,
        FeatureDvdPlusRW,
        FeatureDvdPlusR,
        FeatureBDWrite,
        FeatureHDDVDWrite,
    };
    WCHAR szRoot[] = L"A:\\";
    WCHAR szDevice[] = L"\\\\.\\A:";
    HANDLE hDevice;
    BOOL bRecorder = FALSE;

    if (iDrive < 0 || iDrive >= 26)
        return FALSE;

    szRoot[0] += iDrive;
    if (GetDriveTypeW(szRoot) != DRIVE_CDROM)
        return FALSE;

    szDevice[4] += iDrive;
    hDevice = CreateFileW(szDevice, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
        return FALSE;

    for (UINT i = 0; i < _countof(WriteFeatures) && !bRecorder; i++)
        bRecorder = CDBurn_HasWriteFeature(hDevice, WriteFeatures[i]);

    CloseHandle(hDevice);
    return bRecorder;
}

static int CDBurn_FindRecorder()
{
    DWORD dwDrives = GetLogicalDrives();

    for (int i = 0; i < 26; i++)
    {
        if ((dwDrives & (1 << i)) && CDBurn_IsRecorder(i))
            return i;
    }
    return -1;
}

class CCDBurn :
    public CComCoClass<CCDBurn, &CLSID_CDBurn>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public ICDBurn,
    public IDriveFolderExt,
    public IContextMenu,
    public IDropTarget,
    public INamespaceWalkCB,
    public CObjectWithSiteBase,
    public IPersistFile,
    public IPersistPropertyBag,
    public IQueryCancelAutoPlay,
    public IServiceProvider,
    public IShellExtInit,
    public IShellPropSheetExt
{
    int m_iDrive;
    CComPtr<IDropTarget> m_pStagingTarget;

    HRESULT SetDrive(int iDrive)
    {
        if (!CDBurn_IsRecorder(iDrive))
            return E_FAIL;
        m_iDrive = iDrive;
        return S_OK;
    }

    HRESULT GetStagingDropTarget(HWND hwnd, IDropTarget **ppdt)
    {
        CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidl;
        CComPtr<IShellFolder> psf;
        HRESULT hr;

        hr = SHGetKnownFolderIDList(FOLDERID_CDBurning, KF_FLAG_CREATE, NULL, &pidl);
        if (FAILED(hr))
            return hr;
        hr = SHBindToObject(NULL, pidl, NULL, IID_PPV_ARG(IShellFolder, &psf));
        if (FAILED(hr))
            return hr;
        return psf->CreateViewObject(hwnd, IID_PPV_ARG(IDropTarget, ppdt));
    }

public:
    CCDBurn() : m_iDrive(-1) {}

    STDMETHOD(GetRecorderDriveLetter)(LPWSTR pszDrive, UINT cch) override
    {
        int iDrive;

        if (cch < 4)
            return E_INVALIDARG;

        iDrive = CDBurn_FindRecorder();
        if (iDrive < 0)
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

        PathBuildRootW(pszDrive, iDrive);
        return S_OK;
    }

    STDMETHOD(Burn)(HWND hwnd) override
    {
        if (CDBurn_FindRecorder() < 0)
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

        FIXME("(%p) no disc mastering engine to write the staging area\n", hwnd);
        return E_NOTIMPL;
    }

    STDMETHOD(HasRecordableDrive)(BOOL *pfHasRecorder) override
    {
        *pfHasRecorder = CDBurn_FindRecorder() >= 0;
        return S_OK;
    }

    STDMETHOD(Initialize)(int iDrive) override
    {
        return SetDrive(iDrive);
    }

    STDMETHOD(DriveSupportsBurning)(int iDrive) override
    {
        return CDBurn_IsRecorder(iDrive) ? S_OK : E_FAIL;
    }

    STDMETHOD(Bind)(PCIDLIST_ABSOLUTE pidl, IBindCtx *pbc, REFIID riid, void **ppv) override
    {
        *ppv = NULL;
        if (!pidl)
            return E_INVALIDARG;

        FIXME("(%p %p %s) binding into the disc layout\n", pidl, pbc, debugstr_guid(&riid));
        return E_NOTIMPL;
    }

    STDMETHOD(GetSpace)(ULONGLONG *pullFree, ULONGLONG *pullTotal) override
    {
        if (m_iDrive < 0)
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

        FIXME("(%p %p) disc capacity of drive %d\n", pullFree, pullTotal, m_iDrive);
        return E_NOTIMPL;
    }

    STDMETHOD(QueryContextMenu)(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override
    {
        return S_OK;
    }

    STDMETHOD(InvokeCommand)(LPCMINVOKECOMMANDINFO lpici) override
    {
        return E_FAIL;
    }

    STDMETHOD(GetCommandString)(UINT_PTR idCmd, UINT uType, UINT *pwReserved, LPSTR pszName, UINT cchMax) override
    {
        return E_FAIL;
    }

    STDMETHOD(DragEnter)(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override
    {
        HRESULT hr;

        m_pStagingTarget.Release();
        if (!pDataObj)
        {
            *pdwEffect = DROPEFFECT_NONE;
            return E_NOINTERFACE;
        }

        hr = GetStagingDropTarget(NULL, &m_pStagingTarget);
        if (FAILED(hr))
        {
            *pdwEffect = DROPEFFECT_NONE;
            return hr;
        }
        return m_pStagingTarget->DragEnter(pDataObj, grfKeyState, pt, pdwEffect);
    }

    STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override
    {
        if (!m_pStagingTarget)
            return E_FAIL;
        return m_pStagingTarget->DragOver(grfKeyState, pt, pdwEffect);
    }

    STDMETHOD(DragLeave)() override
    {
        HRESULT hr;

        if (!m_pStagingTarget)
            return E_FAIL;
        hr = m_pStagingTarget->DragLeave();
        m_pStagingTarget.Release();
        return hr;
    }

    STDMETHOD(Drop)(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override
    {
        HRESULT hr;

        if (!m_pStagingTarget)
        {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }
        hr = m_pStagingTarget->Drop(pDataObj, grfKeyState, pt, pdwEffect);
        m_pStagingTarget.Release();
        return hr;
    }

    STDMETHOD(FoundItem)(IShellFolder *psf, PCUITEMID_CHILD pidl) override
    {
        return S_OK;
    }

    STDMETHOD(EnterFolder)(IShellFolder *psf, PCUITEMID_CHILD pidl) override
    {
        return S_OK;
    }

    STDMETHOD(LeaveFolder)(IShellFolder *psf, PCUITEMID_CHILD pidl) override
    {
        return S_OK;
    }

    STDMETHOD(InitializeProgressDialog)(LPWSTR *ppszTitle, LPWSTR *ppszCancel) override
    {
        *ppszTitle = NULL;
        *ppszCancel = NULL;
        return E_NOTIMPL;
    }

    STDMETHOD(GetClassID)(CLSID *pClassID) override
    {
        if (!pClassID)
            return E_POINTER;
        *pClassID = CLSID_CDBurn;
        return S_OK;
    }

    STDMETHOD(IsDirty)() override
    {
        return S_FALSE;
    }

    STDMETHOD(Load)(LPCOLESTR pszFileName, DWORD dwMode) override
    {
        return S_OK;
    }

    STDMETHOD(Save)(LPCOLESTR pszFileName, BOOL fRemember) override
    {
        return S_OK;
    }

    STDMETHOD(SaveCompleted)(LPCOLESTR pszFileName) override
    {
        return S_OK;
    }

    STDMETHOD(GetCurFile)(LPOLESTR *ppszFileName) override
    {
        *ppszFileName = NULL;
        return S_OK;
    }

    STDMETHOD(InitNew)() override
    {
        return S_OK;
    }

    STDMETHOD(Load)(IPropertyBag *pPropBag, IErrorLog *pErrorLog) override
    {
        return S_OK;
    }

    STDMETHOD(Save)(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(AllowAutoPlay)(LPCWSTR pszPath, DWORD dwContentType, LPCWSTR pszLabel, DWORD dwSerialNumber) override
    {
        return S_OK;
    }

    STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void **ppvObject) override
    {
        *ppvObject = NULL;
        return E_FAIL;
    }

    STDMETHOD(Initialize)(PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pdtobj, HKEY hkeyProgID) override
    {
        WCHAR szPath[MAX_PATH];

        if (!pidlFolder && !pdtobj)
            return E_INVALIDARG;

        if (pidlFolder)
        {
            if (!SHGetPathFromIDListW(pidlFolder, szPath))
                return E_FAIL;
        }
        else
        {
            CDataObjectHIDA cida(pdtobj);
            CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidl;

            if (FAILED(cida.hr()) || cida->cidl < 1)
                return E_FAIL;
            pidl.Attach(ILCombine(HIDA_GetPIDLFolder(cida), HIDA_GetPIDLItem(cida, 0)));
            if (!pidl || !SHGetPathFromIDListW(pidl, szPath))
                return E_FAIL;
        }

        if (!PathIsRootW(szPath))
            return E_FAIL;
        return SetDrive(PathGetDriveNumberW(szPath));
    }

    STDMETHOD(AddPages)(LPFNSVADDPROPSHEETPAGE pfnAddPage, LPARAM lParam) override
    {
        if (m_iDrive < 0)
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

        FIXME("(%p %p) recording page of drive %d\n", pfnAddPage, (void *)lParam, m_iDrive);
        return E_NOTIMPL;
    }

    STDMETHOD(ReplacePage)(EXPPS uPageID, LPFNSVADDPROPSHEETPAGE pfnReplaceWith, LPARAM lParam) override
    {
        return S_OK;
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_CDBURN)
    DECLARE_NOT_AGGREGATABLE(CCDBurn)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CCDBurn)
        COM_INTERFACE_ENTRY_IID(IID_ICDBurn, ICDBurn)
        COM_INTERFACE_ENTRY_IID(IID_IDriveFolderExt, IDriveFolderExt)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
        COM_INTERFACE_ENTRY_IID(IID_IDropTarget, IDropTarget)
        COM_INTERFACE_ENTRY_IID(IID_INamespaceWalkCB, INamespaceWalkCB)
        COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFile, IPersistFile)
        COM_INTERFACE_ENTRY_IID(IID_IPersistPropertyBag, IPersistPropertyBag)
        COM_INTERFACE_ENTRY_IID(IID_IQueryCancelAutoPlay, IQueryCancelAutoPlay)
        COM_INTERFACE_ENTRY_IID(IID_IServiceProvider, IServiceProvider)
        COM_INTERFACE_ENTRY_IID(IID_IShellExtInit, IShellExtInit)
        COM_INTERFACE_ENTRY_IID(IID_IShellPropSheetExt, IShellPropSheetExt)
    END_COM_MAP()
};

OBJECT_ENTRY_AUTO(CLSID_CDBurn, CCDBurn)
