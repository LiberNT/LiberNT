/*
 *    Virtual Workplace folder
 *
 *    Copyright 1997                Marcus Meissner
 *    Copyright 1998, 1999, 2002    Juergen Schmied
 *    Copyright 2009                Andrew Hill
 *    Copyright 2017-2024           Katayama Hirofumi MZ
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <precomp.h>
#include <propkey.h>
#include <process.h>

EXTERN_C HRESULT CFSPropertyStoreFactory_CreateInstance(PCIDLIST_ABSOLUTE pidlFolder, IShellFolder2 *psf,
                                                        PCUITEMID_CHILD pidl, PCWSTR pszPath, REFIID riid, void **ppv);

WINE_DEFAULT_DEBUG_CHANNEL(shell);

/*
CDrivesFolder should create a CRegFolder to represent the virtual items that exist only in
the registry. The CRegFolder is aggregated by the CDrivesFolder.
The CDrivesFolderEnum class should enumerate only drives on the system. Since the CRegFolder
implementation of IShellFolder::EnumObjects enumerates the virtual items, the
CDrivesFolderEnum is only responsible for returning the physical items.

2. At least on my XP system, the drive pidls returned are of type PT_DRIVE1, not PT_DRIVE
3. The parsing name returned for my computer is incorrect. It should be "My Computer"
*/

static int iDriveIconIds[7] = { IDI_SHELL_NOT_CONNECTED_HDD, /* DRIVE_UNKNOWN */
                                IDI_SHELL_NOT_CONNECTED_HDD, /* DRIVE_NO_ROOT_DIR*/
                                IDI_SHELL_REMOVEABLE,        /* DRIVE_REMOVABLE*/
                                IDI_SHELL_DRIVE,             /* DRIVE_FIXED*/
                                IDI_SHELL_NETDRIVE,          /* DRIVE_REMOTE*/
                                IDI_SHELL_CDROM,             /* DRIVE_CDROM*/
                                IDI_SHELL_RAMDISK            /* DRIVE_RAMDISK*/
                                };

static int iDriveTypeIds[7] = { IDS_DRIVE_FIXED,             /* DRIVE_UNKNOWN */
                                IDS_DRIVE_FIXED,             /* DRIVE_NO_ROOT_DIR*/
                                IDS_DRIVE_REMOVABLE,         /* DRIVE_REMOVABLE*/
                                IDS_DRIVE_FIXED,             /* DRIVE_FIXED*/
                                IDS_DRIVE_NETWORK,           /* DRIVE_REMOTE*/
                                IDS_DRIVE_CDROM,             /* DRIVE_CDROM*/
                                IDS_DRIVE_FIXED /* FIXME */  /* DRIVE_RAMDISK*/
                                };

static const REQUIREDREGITEM g_RequiredItems[] =
{
    { CLSID_ControlPanel, NULL, REGITEMORDER_MYCOMPUTER_CONTROLS },
};
static const REGFOLDERINFO g_RegFolderInfo =
{
    PT_COMPUTER_REGITEM,
    _countof(g_RequiredItems), g_RequiredItems,
    CLSID_MyComputer,
    L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",
    L"MyComputer",
};

static BOOL IsDelegateItem(PCUIDLIST_RELATIVE pidl)
{
    PCDELEGATEITEMID item = (PCDELEGATEITEMID)pidl;

    return pidl && pidl->mkid.cb >= FIELD_OFFSET(DELEGATEITEMID, rgb) + sizeof(CLSID) &&
           item->wOuter == PT_DELEGATE &&
           FIELD_OFFSET(DELEGATEITEMID, rgb) + item->cbInner + sizeof(CLSID) == item->cbSize;
}

class CDelegateMalloc : public IMalloc
{
    LONG m_cRef;
    CLSID m_clsid;

public:
    CDelegateMalloc(REFCLSID clsid) : m_cRef(1), m_clsid(clsid) {}
    virtual ~CDelegateMalloc() {}

    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IMalloc)
        {
            *ppv = static_cast<IMalloc *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override
    {
        return InterlockedIncrement(&m_cRef);
    }
    STDMETHOD_(ULONG, Release)() override
    {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (!cRef)
            delete this;
        return cRef;
    }
    STDMETHOD_(void *, Alloc)(SIZE_T cb) override
    {
        SIZE_T size = FIELD_OFFSET(DELEGATEITEMID, rgb) + cb + sizeof(CLSID);
        if (size > 0xFFFF)
            return NULL;
        DELEGATEITEMID *item = (DELEGATEITEMID *)SHAlloc(size + sizeof(USHORT));
        if (!item)
            return NULL;
        ZeroMemory(item, size + sizeof(USHORT));
        item->cbSize = (WORD)size;
        item->wOuter = PT_DELEGATE;
        item->cbInner = (WORD)cb;
        CopyMemory((BYTE *)item + size - sizeof(CLSID), &m_clsid, sizeof(CLSID));
        return item;
    }
    STDMETHOD_(void *, Realloc)(void *pv, SIZE_T cb) override
    {
        return NULL;
    }
    STDMETHOD_(void, Free)(void *pv) override
    {
        SHFree(pv);
    }
    STDMETHOD_(SIZE_T, GetSize)(void *pv) override
    {
        return pv ? ((PCDELEGATEITEMID)pv)->cbSize : 0;
    }
    STDMETHOD_(int, DidAlloc)(void *pv) override
    {
        return pv ? IsDelegateItem((PCUIDLIST_RELATIVE)pv) : -1;
    }
    STDMETHOD_(void, HeapMinimize)() override
    {
    }
};

static const CLSID* IsRegItem(PCUITEMID_CHILD pidl)
{
    if (pidl && pidl->mkid.cb == 2 + 2 + sizeof(CLSID))
    {
        if (pidl->mkid.abID[0] == PT_SHELLEXT || pidl->mkid.abID[0] == PT_GUID) // FIXME: Remove PT_GUID when CRegFolder is fixed
            return (const CLSID*)(&pidl->mkid.abID[2]);
    }
    return NULL;
}

static bool IsRegItem(PCUITEMID_CHILD pidl, REFCLSID clsid)
{
    const CLSID *pClass = IsRegItem(pidl);
    return pClass && *pClass == clsid;
}

static bool IsSimpleDriveItem(PCUITEMID_CHILD pidl)
{
    return _ILIsDrive(pidl) && _ILIsPidlSimple(pidl);
}

static bool IsFileSystemFolderItem(PCUITEMID_CHILD pidl)
{
    return _ILIsDrive(pidl) && !_ILIsPidlSimple(pidl);
}

static HRESULT GetFileSystemFolderUIObject(PCIDLIST_ABSOLUTE root, HWND hwndOwner, PCUITEMID_CHILD pidl,
                                           REFIID riid, UINT *reserved, void **object)
{
    CComHeapPtr<ITEMIDLIST> absolute(ILCombine(root, pidl));
    if (!absolute)
        return E_OUTOFMEMORY;

    CComPtr<IShellFolder> parent;
    PCUITEMID_CHILD child;
    HRESULT hr = SHBindToParent(absolute, IID_PPV_ARG(IShellFolder, &parent), &child);
    if (FAILED(hr))
        return hr;
    return parent->GetUIObjectOf(hwndOwner, 1, &child, riid, reserved, object);
}

static INT8 GetDriveNumber(PCUITEMID_CHILD pidl)
{
    if (!_ILIsDrive(pidl))
        return -1;
    BYTE letter = ((PIDLDATA*)pidl->mkid.abID)->u.drive.szDriveName[0];
    BYTE number = (letter | 32) - 'a';
    return number < 26 ? number : -1;
}

static INT8 GetDriveNumber(PCWSTR DrivePath)
{
    WORD number = (DrivePath[0] | 32) - 'a';
    return number < 26 ? number : -1;
}

UINT g_IsFloppyCache = 0;

static UINT GetCachedDriveType(INT8 DrvNum)
{
    if (DrvNum < 0 || DrvNum >= 26)
        return DRIVE_UNKNOWN;
    if (g_IsFloppyCache & (1 << DrvNum))
        return DRIVE_REMOVABLE;
    WCHAR DrvPath[] = { LOWORD('A' + DrvNum), ':', '\\', '\0' };
    return ::GetDriveTypeW(DrvPath);
}

static inline UINT GetCachedDriveType(PCWSTR DrivePath)
{
    return GetCachedDriveType(GetDriveNumber(DrivePath));
}

static bool IsFloppyDrive(PCWSTR DrivePath)
{
    extern BOOL IsDriveFloppyW(LPCWSTR pszDriveRoot);
    INT8 DrvNum = GetDriveNumber(DrivePath);
    if (DrvNum < 0)
        return false;
    if (g_IsFloppyCache & (1 << DrvNum))
        return true;
    UINT Type = GetCachedDriveType(DrvNum);
    if (Type == DRIVE_REMOVABLE && IsDriveFloppyW(DrivePath))
    {
        g_IsFloppyCache |= (1 << DrvNum);
        return true;
    }
    return false;
}

INT8 GetDrivePath(PCUITEMID_CHILD pidl, PWSTR Path)
{
    INT8 number = GetDriveNumber(pidl);
    if (number < 0)
        return number;
    Path[0] = 'A' + number;
    Path[1] = ':';
    Path[2] = '\\';
    Path[3] = '\0';
    return number;
}

static inline UINT _ILGetRemovableTypeId(LPCITEMIDLIST pidl)
{
    WCHAR buf[8];
    if (GetDrivePath(pidl, buf) >= 0 && IsFloppyDrive(buf))
        return SHDID_COMPUTER_DRIVE35; // TODO: 3.5-inch vs 5.25-inch
    return SHDID_COMPUTER_REMOVABLE;
}

UINT _ILGetDriveType(LPCITEMIDLIST pidl)
{
    WCHAR szDrive[8];
    if (!_ILGetDrive(pidl, szDrive, _countof(szDrive)))
    {
        ERR("pidl %p is not a drive\n", pidl);
        return DRIVE_UNKNOWN;
    }
    return GetCachedDriveType(GetDriveNumber(szDrive));
}

static UINT SHELL_GetAutoRunInfPath(PCWSTR DrvPath, PWSTR AriPath, BOOL ForInvoke = FALSE)
{
    INT8 DrvNum = GetDriveNumber(DrvPath);
    if (DrvNum < 0 || DrvPath[1] != ':' || (DrvPath[2] && (DrvPath[2] != '\\' || DrvPath[3])))
        return 0;
    if (!ForInvoke && IsFloppyDrive(DrvPath))
        return 0; // Don't read label nor icon from floppy
    if (GetCachedDriveType(DrvNum) <= DRIVE_NO_ROOT_DIR)
        return 0;
    return wsprintfW(AriPath, L"%c:\\autorun.inf", DrvPath[0]);
}

#if 0 // TODO: Call this when the shell is notified about insert disc events
bool SHELL_CanInvokeAutoRunOnDrive(PCWSTR DrvPath)
{
    INT8 DrvNum = GetDriveNumber(DrvPath);
    if (DrvNum < 0 || (SHRestricted(REST_NODRIVEAUTORUN) & (1 << DrvNum)))
        return false;
    return !(SHRestricted(REST_NODRIVETYPEAUTORUN) & (1 << GetCachedDriveType(DrvNum)));
}
#endif

/**
 * @brief
 * Returns true if the item should be hidden in DefView but not in the Explorer folder tree.
 **/
BOOL SHELL32_IsShellFolderNamespaceItemHidden(LPCWSTR SubKey, REFCLSID Clsid)
{
    WCHAR path[MAX_PATH], name[CHARS_IN_GUID];
    wsprintfW(path, L"%s\\%s", REGSTR_PATH_EXPLORER, SubKey);
    SHELL32_GUIDToStringW(Clsid, name);
    // Check the value in both HKEY_CURRENT_USER and HKEY_LOCAL_MACHINE
    return SHRegGetBoolUSValue(path, name, FALSE, FALSE);
}

/***********************************************************************
*   IShellFolder implementation
*/

#define RETRY_COUNT 3
#define RETRY_SLEEP 250
static BOOL TryToLockOrUnlockDrive(HANDLE hDrive, BOOL bLock)
{
    DWORD dwError, dwBytesReturned;
    DWORD dwCode = (bLock ? FSCTL_LOCK_VOLUME : FSCTL_UNLOCK_VOLUME);
    for (DWORD i = 0; i < RETRY_COUNT; ++i)
    {
        if (DeviceIoControl(hDrive, dwCode, NULL, 0, NULL, 0, &dwBytesReturned, NULL))
            return TRUE;

        dwError = GetLastError();
        if (dwError == ERROR_INVALID_FUNCTION)
            break; /* don't sleep if function is not implemented */

        Sleep(RETRY_SLEEP);
    }
    SetLastError(dwError);
    return FALSE;
}

// NOTE: See also https://support.microsoft.com/en-us/help/165721/how-to-ejecting-removable-media-in-windows-nt-windows-2000-windows-xp
static BOOL DoEjectDrive(const WCHAR *physical, UINT nDriveType, INT *pnStringID)
{
    /* GENERIC_WRITE isn't needed for umount */
    DWORD dwAccessMode = GENERIC_READ;
    DWORD dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;

    HANDLE hDrive = CreateFile(physical, dwAccessMode, dwShareMode, 0, OPEN_EXISTING, 0, NULL);
    if (hDrive == INVALID_HANDLE_VALUE)
        return FALSE;

    BOOL bResult, bNeedUnlock = FALSE;
    DWORD dwBytesReturned, dwError = NO_ERROR;
    PREVENT_MEDIA_REMOVAL removal;
    do
    {
        bResult = TryToLockOrUnlockDrive(hDrive, TRUE);
        if (!bResult)
        {
            dwError = GetLastError();
            *pnStringID = IDS_CANTLOCKVOLUME; /* Unable to lock volume */
            break;
        }
        bResult = DeviceIoControl(hDrive, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &dwBytesReturned, NULL);
        if (!bResult)
        {
            dwError = GetLastError();
            *pnStringID = IDS_CANTDISMOUNTVOLUME; /* Unable to dismount volume */
            bNeedUnlock = TRUE;
            break;
        }
        removal.PreventMediaRemoval = FALSE;
        bResult = DeviceIoControl(hDrive, IOCTL_STORAGE_MEDIA_REMOVAL, &removal, sizeof(removal), NULL,
                                  0, &dwBytesReturned, NULL);
        if (!bResult)
        {
            *pnStringID = IDS_CANTEJECTMEDIA; /* Unable to eject media */
            dwError = GetLastError();
            bNeedUnlock = TRUE;
            break;
        }
        bResult = DeviceIoControl(hDrive, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &dwBytesReturned, NULL);
        if (!bResult)
        {
            *pnStringID = IDS_CANTEJECTMEDIA; /* Unable to eject media */
            dwError = GetLastError();
            bNeedUnlock = TRUE;
            break;
        }
    } while (0);

    if (bNeedUnlock)
    {
        TryToLockOrUnlockDrive(hDrive, FALSE);
    }

    CloseHandle(hDrive);

    SetLastError(dwError);
    return bResult;
}

static DWORD CALLBACK DoFormatDriveThread(LPVOID args)
{
    UINT nDrive = PtrToUlong(args);
    WCHAR szPath[] = { LOWORD(L'A' + nDrive), L'\0' }; // Arbitrary, just needs to include nDrive
    CStubWindow32 stub;
    HRESULT hr = stub.CreateStub(CStubWindow32::TYPE_FORMATDRIVE, szPath, NULL);
    if (FAILED(hr))
        return hr;
    SHFormatDrive(stub, nDrive, SHFMT_ID_DEFAULT, 0);
    return stub.DestroyWindow();
}

static HRESULT DoFormatDriveAsync(HWND hwnd, UINT nDrive)
{
    BOOL succ = SHCreateThread(DoFormatDriveThread, UlongToPtr(nDrive), CTF_PROCESS_REF, NULL);
    return succ ? S_OK : E_FAIL;
}

enum { CMDID_FORMAT, CMDID_EJECT, CMDID_DISCONNECT };
static const CMVERBMAP g_VerbMap[] =
{
    { "format", CMDID_FORMAT },
    { "eject", CMDID_EJECT },
    { "disconnect", CMDID_DISCONNECT },
    { NULL }
};

HRESULT CALLBACK DrivesContextMenuCallback(IShellFolder *psf,
                                           HWND         hwnd,
                                           IDataObject  *pdtobj,
                                           UINT         uMsg,
                                           WPARAM       wParam,
                                           LPARAM       lParam)
{
    if (uMsg != DFM_MERGECONTEXTMENU && uMsg != DFM_INVOKECOMMAND && uMsg != DFM_MAPCOMMANDNAME)
        return SHELL32_DefaultContextMenuCallBack(psf, pdtobj, uMsg);

    PIDLIST_ABSOLUTE pidlFolder;
    PUITEMID_CHILD *apidl;
    UINT cidl;
    DWORD dwFlags;
    HRESULT hr = SH_GetApidlFromDataObject(pdtobj, &pidlFolder, &apidl, &cidl);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    WCHAR szDrive[8] = {0};
    if (!_ILGetDrive(apidl[0], szDrive, _countof(szDrive)))
    {
        ERR("pidl is not a drive\n");
        SHFree(pidlFolder);
        _ILFreeaPidl(apidl, cidl);
        return E_FAIL;
    }
    UINT nDriveType = GetCachedDriveType(szDrive);
    if (!GetVolumeInformationW(szDrive, NULL, 0, NULL, NULL, &dwFlags, NULL, 0))
    {
        if (nDriveType >= DRIVE_REMOTE)
            dwFlags = FILE_READ_ONLY_VOLUME;
        else
            dwFlags = 0; // Assume drive with unknown filesystem, allow format
    }

    if (uMsg == DFM_MERGECONTEXTMENU)
    {
        QCMINFO *pqcminfo = (QCMINFO *)lParam;

        UINT idCmdFirst = pqcminfo->idCmdFirst;
        UINT idCmd = 0;
        if (!(dwFlags & FILE_READ_ONLY_VOLUME) && nDriveType != DRIVE_REMOTE && cidl == 1)
        {
            /* add separator and Format */
            idCmd = idCmdFirst + CMDID_FORMAT;
            _InsertMenuItemW(pqcminfo->hmenu, pqcminfo->indexMenu++, TRUE, 0, MFT_SEPARATOR, NULL, 0);
            _InsertMenuItemW(pqcminfo->hmenu, pqcminfo->indexMenu++, TRUE, idCmd, MFT_STRING, MAKEINTRESOURCEW(IDS_FORMATDRIVE), MFS_ENABLED);
        }
        if (nDriveType == DRIVE_REMOVABLE || nDriveType == DRIVE_CDROM)
        {
            /* add separator and Eject */
            idCmd = idCmdFirst + CMDID_EJECT;
            _InsertMenuItemW(pqcminfo->hmenu, pqcminfo->indexMenu++, TRUE, 0, MFT_SEPARATOR, NULL, 0);
            _InsertMenuItemW(pqcminfo->hmenu, pqcminfo->indexMenu++, TRUE, idCmd, MFT_STRING, MAKEINTRESOURCEW(IDS_EJECT), MFS_ENABLED);
        }
        if (nDriveType == DRIVE_REMOTE)
        {
            /* add separator and Disconnect */
            idCmd = idCmdFirst + CMDID_DISCONNECT;
            _InsertMenuItemW(pqcminfo->hmenu, pqcminfo->indexMenu++, TRUE, 0, MFT_SEPARATOR, NULL, 0);
            _InsertMenuItemW(pqcminfo->hmenu, pqcminfo->indexMenu++, TRUE, idCmd, MFT_STRING, MAKEINTRESOURCEW(IDS_DISCONNECT), MFS_ENABLED);
        }

        if (idCmd)
            pqcminfo->idCmdFirst = (idCmd + 2); // Note: This assumes the items above are added in ascending id order.
                                                // CORE-18577: We add 2 instead of 1 because the other is for our separator!
                                                // Our CDefaultContextMenu::QueryContextMenu incorrectly assumes
                                                // all DFM_MERGECONTEXTMENU added items have IDs.
        hr = S_OK;
    }
    else if (uMsg == DFM_INVOKECOMMAND)
    {
        WCHAR wszBuf[4] = L"A:\\";
        wszBuf[0] = (WCHAR)szDrive[0];

        INT nStringID = 0;
        DWORD dwError = NO_ERROR;

        if (wParam == DFM_CMD_PROPERTIES)
        {
            ATLASSERT(pdtobj);
            hr = SHELL32_ShowFilesystemItemPropertiesDialogAsync(pdtobj);
            // Not setting nStringID because SHOpenPropSheet already displayed an error box
        }
        else
        {
            if (wParam == CMDID_FORMAT)
            {
                hr = DoFormatDriveAsync(hwnd, szDrive[0] - 'A');
            }
            else if (wParam == CMDID_EJECT)
            {
                /* do eject */
                WCHAR physical[10];
                wsprintfW(physical, _T("\\\\.\\%c:"), szDrive[0]);

                if (DoEjectDrive(physical, nDriveType, &nStringID))
                {
                    SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, wszBuf, NULL);
                }
                else
                {
                    dwError = GetLastError();
                }
            }
            else if (wParam == CMDID_DISCONNECT)
            {
                /* do disconnect */
                wszBuf[2] = UNICODE_NULL;
                dwError = WNetCancelConnection2W(wszBuf, 0, FALSE);
                if (dwError == NO_ERROR)
                {
                    SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, wszBuf, NULL);
                }
                else
                {
                    nStringID = IDS_CANTDISCONNECT;
                }
            }
            else if (hr == S_OK)
            {
                hr = E_INVALIDARG; // Don't eat DFM_CMD_* in CDefaultContextMenu::Do*
            }
        }

        if (nStringID != 0)
        {
            /* show error message */
            WCHAR szFormat[128], szMessage[128];
            LoadStringW(shell32_hInstance, nStringID, szFormat, _countof(szFormat));
            wsprintfW(szMessage, szFormat, dwError);
            MessageBoxW(hwnd, szMessage, NULL, MB_ICONERROR);
        }
    }
    else if (uMsg == DFM_MAPCOMMANDNAME)
    {
        CMINVOKECOMMANDINFOEX ici = { sizeof(ici), CMIC_MASK_UNICODE };
        ici.lpVerbW = (PWSTR)lParam;
        if (SUCCEEDED(hr = SHELL_MapContextMenuVerbToCmdId((CMINVOKECOMMANDINFO*)&ici, g_VerbMap)))
        {
            *(int*)wParam = hr;
            hr = S_OK;
        }
    }

    SHFree(pidlFolder);
    _ILFreeaPidl(apidl, cidl);

    return hr;
}

HRESULT CDrivesContextMenu_CreateInstance(PCIDLIST_ABSOLUTE pidlFolder,
                                          HWND hwnd,
                                          UINT cidl,
                                          PCUITEMID_CHILD_ARRAY apidl,
                                          IShellFolder *psf,
                                          IContextMenu **ppcm)
{
    CRegKeyHandleArray keys;
    AddClassKeyToArray(L"Drive", keys, keys);
    AddClassKeyToArray(L"Folder", keys, keys);
    return CDefFolderMenu_Create2(pidlFolder, hwnd, cidl, apidl, psf, DrivesContextMenuCallback, keys, keys, ppcm);
}

static HRESULT
getIconLocationForDrive(
    _In_ PCWSTR pszDrivePath,
    _Out_writes_z_(cchMax) PWSTR szIconFile,
    _In_ UINT cchMax,
    _Out_ int *piIndex,
    _Out_ UINT *pGilOut)
{
    WCHAR wszPath[MAX_PATH];
    WCHAR wszAutoRunInfPath[MAX_PATH];
    WCHAR wszValue[MAX_PATH], wszTemp[MAX_PATH];

    if (!SHELL_GetAutoRunInfPath(pszDrivePath, wszAutoRunInfPath))
        return E_FAIL;
    wcscpy(wszPath, pszDrivePath);

    // autorun.inf --> wszValue
    if (GetPrivateProfileStringW(L"autorun", L"icon", NULL, wszValue, _countof(wszValue),
                                 wszAutoRunInfPath) && wszValue[0] != 0)
    {
        // wszValue --> wszTemp
        ExpandEnvironmentStringsW(wszValue, wszTemp, _countof(wszTemp));

        *piIndex = PathParseIconLocationW(wszTemp);

        // wszPath + wszTemp --> wszPath
        if (PathIsRelativeW(wszTemp))
            PathAppendW(wszPath, wszTemp);
        else
            StringCchCopyW(wszPath, _countof(wszPath), wszTemp);

        // wszPath --> szIconFile
        GetFullPathNameW(wszPath, cchMax, szIconFile, NULL);
        return S_OK;
    }
    return E_FAIL;
}

static HRESULT
getLabelForDriveFromAutoRun(PCWSTR wszPath, LPWSTR szLabel, UINT cchMax)
{
    WCHAR wszAutoRunInfPath[MAX_PATH];
    WCHAR wszTemp[MAX_PATH];

    if (!SHELL_GetAutoRunInfPath(wszPath, wszAutoRunInfPath))
        return E_FAIL;

    if (GetPrivateProfileStringW(L"autorun", L"label", NULL, wszTemp, _countof(wszTemp),
                                 wszAutoRunInfPath) && wszTemp[0] != 0)
    {
        return StringCchCopyW(szLabel, cchMax, wszTemp);
    }
    return E_FAIL;
}

static inline HRESULT GetRawDriveLabel(PCWSTR DrivePath, LPWSTR szLabel, UINT cchMax)
{
    if (GetVolumeInformationW(DrivePath, szLabel, cchMax, NULL, NULL, NULL, NULL, 0))
        return *szLabel ? S_OK : S_FALSE;
    return HResultFromWin32(GetLastError());
}

static HRESULT GetDriveLabel(PCWSTR DrivePath, LPWSTR szLabel, UINT cchMax)
{
    HRESULT hr = getLabelForDriveFromAutoRun(DrivePath, szLabel, cchMax);
    return hr == S_OK ? S_OK : GetRawDriveLabel(DrivePath, szLabel, cchMax);
}

static bool GetRegCustomizedDriveIcon(
    _In_ INT8 DriveNum,
    _Out_writes_z_(cchMax) PWSTR szIconFile,
    _In_ UINT cchMax,
    _Out_ int *piIndex,
    _Out_ UINT *pGilOut)
{
    WCHAR szKey[200], chDrv = 'A' + DriveNum;
    wsprintfW(szKey, L"%s\\DriveIcons\\%c\\DefaultIcon", REGSTR_PATH_EXPLORER, chDrv);
    DWORD cb = cchMax * sizeof(*szIconFile);
    DWORD err = RegGetValueW(HKEY_LOCAL_MACHINE, szKey, NULL, RRF_RT_REG_SZ, NULL, szIconFile, &cb);
    if (err != ERROR_SUCCESS)
    {
        cb = cchMax * sizeof(*szIconFile);
        wsprintfW(szKey, L"Applications\\Explorer.exe\\Drives\\%c\\DefaultIcon", chDrv);
        err = RegGetValueW(HKEY_CLASSES_ROOT, szKey, NULL, RRF_RT_REG_SZ, NULL, szIconFile, &cb);
    }
    if (err != ERROR_SUCCESS)
        return false;
    *piIndex = PathParseIconLocationW(szIconFile);
    return *szIconFile != UNICODE_NULL;
}

HRESULT CDrivesExtractIcon_CreateInstance(IShellFolder * psf, LPCITEMIDLIST pidl, REFIID riid, LPVOID * ppvOut)
{
    CComPtr<IDefaultExtractIconInit> initIcon;
    HRESULT hr = SHCreateDefaultExtractIcon(IID_PPV_ARG(IDefaultExtractIconInit, &initIcon));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    WCHAR wTemp[MAX_PATH], szDrive[8];
    int icon_idx, reg_idx = 0;
    UINT GilOut = 0;
    BOOL bFloppy = FALSE;
    INT8 DriveNum = GetDrivePath(pidl, szDrive);
    UINT DriveType = GetCachedDriveType(DriveNum);
    if (DriveType > DRIVE_RAMDISK)
        DriveType = DRIVE_FIXED;

    switch (DriveType)
    {
        case DRIVE_REMOVABLE:
            bFloppy = IsFloppyDrive(szDrive);
            reg_idx = bFloppy ? IDI_SHELL_3_14_FLOPPY : IDI_SHELL_REMOVEABLE;
            break;
        case DRIVE_FIXED:
            reg_idx = IDI_SHELL_DRIVE;
            break;
        case DRIVE_REMOTE:
            reg_idx = IDI_SHELL_NETDRIVE;
            break;
        case DRIVE_CDROM:
            reg_idx = IDI_SHELL_CDROM;
            break;
        case DRIVE_RAMDISK:
            reg_idx = IDI_SHELL_RAMDISK;
            break;
    }

    hr = getIconLocationForDrive(szDrive, wTemp, _countof(wTemp),
                                 &icon_idx, &GilOut);
    if (SUCCEEDED(hr))
    {
        initIcon->SetNormalIcon(wTemp, icon_idx);
    }
    else if (DriveType > DRIVE_NO_ROOT_DIR &&
             GetRegCustomizedDriveIcon(DriveNum, wTemp, _countof(wTemp), &icon_idx, &GilOut))
    {
        initIcon->SetNormalIcon(wTemp, icon_idx);
    }
    else if (reg_idx && HLM_GetIconW(reg_idx - 1, wTemp, _countof(wTemp), &icon_idx))
    {
        initIcon->SetNormalIcon(wTemp, icon_idx);
    }
    else if (DriveType == DRIVE_FIXED &&
             HCR_GetIconW(L"Drive", wTemp, NULL, _countof(wTemp), &icon_idx))
    {
        initIcon->SetNormalIcon(wTemp, icon_idx);
    }
    else
    {
        if (DriveType == DRIVE_REMOVABLE && bFloppy)
            icon_idx = IDI_SHELL_3_14_FLOPPY;
        else
            icon_idx = iDriveIconIds[DriveType];
        initIcon->SetNormalIcon(swShell32Name, -icon_idx);
    }

    return initIcon->QueryInterface(riid, ppvOut);
}


/* LIBERNT_KNOWN_FOLDERS_V2: six direct children of My Computer.
 * Virtual item IDs are never drive-rooted filesystem PIDLs. */
static const BYTE s_KnownFolderPidlTag = 0xFD;

struct KnownFolderEntry
{
    const GUID *id;
    const WCHAR *name;
};

static const KnownFolderEntry s_KnownFolders[] =
{
    { &FOLDERID_Desktop,   L"Desktop" },
    { &FOLDERID_Documents, L"Documents" },
    { &FOLDERID_Downloads, L"Downloads" },
    { &FOLDERID_Music,     L"Music" },
    { &FOLDERID_Pictures,  L"Pictures" },
    { &FOLDERID_Videos,    L"Videos" },
};

static const KnownFolderEntry *FindKnownFolderById(REFGUID id)
{
    for (UINT i = 0; i < _countof(s_KnownFolders); ++i)
        if (IsEqualGUID(id, *s_KnownFolders[i].id))
            return &s_KnownFolders[i];
    return NULL;
}

static const KnownFolderEntry *GetKnownFolderChild(PCUIDLIST_RELATIVE pidl)
{
    if (!pidl || pidl->mkid.cb != sizeof(USHORT) + 1 + sizeof(GUID) ||
        pidl->mkid.abID[0] != s_KnownFolderPidlTag || !_ILIsPidlSimple(pidl))
        return NULL;
    GUID id;
    memcpy(&id, pidl->mkid.abID + 1, sizeof(id));
    return FindKnownFolderById(id);
}

static PIDLIST_RELATIVE CreateKnownFolderChild(REFGUID id)
{
    if (!FindKnownFolderById(id))
        return NULL;
    const USHORT size = sizeof(USHORT) + 1 + sizeof(GUID);
    BYTE *bytes = static_cast<BYTE *>(CoTaskMemAlloc(size + sizeof(USHORT)));
    if (!bytes)
        return NULL;
    memset(bytes, 0, size + sizeof(USHORT));
    memcpy(bytes, &size, sizeof(size));
    bytes[sizeof(USHORT)] = s_KnownFolderPidlTag;
    memcpy(bytes + sizeof(USHORT) + 1, &id, sizeof(GUID));
    return reinterpret_cast<PIDLIST_RELATIVE>(bytes);
}

static HRESULT GetKnownFolderAbsolute(REFGUID id, PIDLIST_ABSOLUTE *absolute)
{
    return SHGetKnownFolderIDList(id, KF_FLAG_CREATE, NULL, absolute);
}

static HRESULT KnownFolderUIObject(HWND hwndOwner, REFGUID id, REFIID riid,
                                   UINT *flags, void **result)
{
    PIDLIST_ABSOLUTE absolute = NULL;
    HRESULT hr = GetKnownFolderAbsolute(id, &absolute);
    if (FAILED(hr))
        return hr;
    CComPtr<IShellFolder> parent;
    PCUITEMID_CHILD child = NULL;
    hr = SHBindToParent(absolute, IID_PPV_ARG(IShellFolder, &parent), &child);
    if (SUCCEEDED(hr))
        hr = parent->GetUIObjectOf(hwndOwner, 1, &child, riid, flags, result);
    ILFree(absolute);
    return hr;
}

/* LIBERNT_KNOWN_FOLDERS_V9_BEGIN */
/* Recognize only the four affected known folders as simple filesystem PIDLs.
 * These items must never be interpreted as drive PIDLs. */
static const KnownFolderEntry *V9KnownPhysicalChild(PCUIDLIST_RELATIVE pidl)
{
    if (!pidl || pidl->mkid.cb < 3 || !_ILIsPidlSimple(pidl) ||
        pidl->mkid.abID[0] != PT_FOLDER || !_ILIsFolder(pidl))
        return NULL;
    WCHAR name[MAX_PATH] = {0};
    if (!_ILSimpleGetTextW(pidl, name, _countof(name)))
        return NULL;
    for (UINT i = 0; i < _countof(s_KnownFolders); ++i)
    {
        const KnownFolderEntry *entry = &s_KnownFolders[i];
        if (IsEqualGUID(*entry->id, FOLDERID_Desktop) ||
            IsEqualGUID(*entry->id, FOLDERID_Documents))
            continue;
        if (!lstrcmpiW(name, entry->name))
            return entry;
    }
    return NULL;
}
/* LIBERNT_KNOWN_FOLDERS_V9_END */

class CDrivesFolderEnum :
    public CEnumIDListBase
{
    HRESULT AddKnownFolder(HWND hwndOwner, CDrivesFolder *folder, REFKNOWNFOLDERID folderId)
    {
        if (FindKnownFolderById(folderId))
        {
            PIDLIST_RELATIVE child = CreateKnownFolderChild(folderId);
            if (!child)
                return E_OUTOFMEMORY;
            if (!AddToEnumList(child))
            {
                ILFree(child);
                return E_OUTOFMEMORY;
            }
            return S_OK;
        }

        PIDLIST_ABSOLUTE absolute = NULL;
        HRESULT hr = SHGetKnownFolderIDList(folderId, KF_FLAG_CREATE, NULL, &absolute);
        if (FAILED(hr))
            return hr;

        WCHAR path[MAX_PATH];
        if (!SHGetPathFromIDListW(absolute, path))
        {
            ILFree(absolute);
            return E_FAIL;
        }
        ILFree(absolute);

        PIDLIST_RELATIVE pidl = NULL;
        hr = folder->ParseDisplayName(hwndOwner, NULL, path, NULL, &pidl, NULL);
        if (FAILED(hr))
            return hr;

        if (!AddToEnumList(pidl))
        {
            ILFree(pidl);
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }

    public:
        HRESULT WINAPI Initialize(HWND hwndOwner, DWORD dwFlags, IEnumIDList* pRegEnumerator, CDrivesFolder *folder)
        {
            /* enumerate the folders */
            if (dwFlags & SHCONTF_FOLDERS)
            {
                const KNOWNFOLDERID *folderIds[] =
                {
                    &FOLDERID_Desktop,
                    &FOLDERID_Documents,
                    &FOLDERID_Downloads,
                    &FOLDERID_Music,
                    &FOLDERID_Pictures,
                    &FOLDERID_Videos,
                };
                for (UINT i = 0; i < _countof(folderIds); ++i)
                    AddKnownFolder(hwndOwner, folder, *folderIds[i]);

            for (UINT i = 0; i < folder->GetDelegateCount(); ++i)
            {
                CComPtr<IEnumIDList> pDelegateEnum;
                if (folder->EnumDelegates(hwndOwner, dwFlags, &pDelegateEnum, i) == S_OK)
                    AppendItemsFromEnumerator(pDelegateEnum);
            }

                WCHAR wszDriveName[] = {'A', ':', '\\', '\0'};
                DWORD dwDrivemap = GetLogicalDrives() & ~SHRestricted(REST_NODRIVES);
                for (; wszDriveName[0] <= 'Z'; wszDriveName[0]++)
                {
                    if ((dwDrivemap & 1) && folder->ShouldEnumDrive(wszDriveName))
                        AddToEnumList(_ILCreateDrive(wszDriveName));
                    dwDrivemap >>= 1;
                }
            }

            /* Enumerate the items of the reg folder */
            AppendItemsFromEnumerator(pRegEnumerator);

            return S_OK;
        }

        BEGIN_COM_MAP(CDrivesFolderEnum)
        COM_INTERFACE_ENTRY_IID(IID_IEnumIDList, IEnumIDList)
        END_COM_MAP()
};

/***********************************************************************
*   IShellFolder [MyComputer] implementation
*/

static const shvheader MyComputerSFHeader[] = {
    {IDS_SHV_COLUMN_NAME, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 15},
    {IDS_SHV_COLUMN_TYPE, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 10},
    {IDS_SHV_COLUMN_DISK_CAPACITY, SHCOLSTATE_TYPE_INT | SHCOLSTATE_ONBYDEFAULT | SHCOLSTATE_SLOW, LVCFMT_RIGHT, 10},
    {IDS_SHV_COLUMN_DISK_AVAILABLE, SHCOLSTATE_TYPE_INT | SHCOLSTATE_ONBYDEFAULT | SHCOLSTATE_SLOW, LVCFMT_RIGHT, 10},
    {IDS_SHV_COLUMN_FILESYSTEM, SHCOLSTATE_TYPE_STR | SHCOLSTATE_SLOW, LVCFMT_LEFT, 10},
    {IDS_SHV_COLUMN_COMMENTS, SHCOLSTATE_TYPE_STR | SHCOLSTATE_SLOW, LVCFMT_LEFT, 10},
    {IDS_SHV_COLUMN_NETLOCATION, SHCOLSTATE_TYPE_STR | SHCOLSTATE_SLOW, LVCFMT_LEFT, 10},
    {IDS_SHV_COLUMN_PERCENTFULL, SHCOLSTATE_TYPE_INT | SHCOLSTATE_SLOW, LVCFMT_LEFT, 10},
    {IDS_SHV_COLUMN_FREESPACE, SHCOLSTATE_TYPE_STR | SHCOLSTATE_SLOW | SHCOLSTATE_HIDDEN, LVCFMT_LEFT, 15},
};

static const SHCOLUMNID MyComputerNetworkLocationScid =
    { { 0x28636aa6, 0x953d, 0x11d2, { 0xb5, 0xd6, 0x00, 0xc0, 0x4f, 0xd9, 0x18, 0xd0 } }, 4 };

static const DWORD dwComputerAttributes =
    SFGAO_CANRENAME | SFGAO_CANDELETE | SFGAO_HASPROPSHEET | SFGAO_DROPTARGET |
    SFGAO_FILESYSANCESTOR | SFGAO_FOLDER | SFGAO_HASSUBFOLDER;
static const DWORD dwControlPanelAttributes =
    SFGAO_HASSUBFOLDER | SFGAO_FOLDER | SFGAO_CANLINK | SFGAO_NONENUMERATED;
static const DWORD dwDriveAttributes =
    SFGAO_HASSUBFOLDER | SFGAO_FILESYSTEM | SFGAO_FOLDER | SFGAO_FILESYSANCESTOR |
    SFGAO_STORAGEANCESTOR | SFGAO_STORAGE |
    SFGAO_DROPTARGET | SFGAO_HASPROPSHEET | SFGAO_CANRENAME | SFGAO_CANLINK | SFGAO_CANCOPY;

CDrivesFolder::CDrivesFolder()
{
    pidlRoot = NULL;
    m_DriveDisplayMode = -1;
    m_cBackReferences = 0;
    m_bHideNonRemovableDrives = FALSE;
    m_bForceAllDrivesRemovable = FALSE;
    m_DelegateCount = 0;
    m_DelegatesLoaded = FALSE;
}

void CDrivesFolder::LoadDelegates()
{
    HKEY hKey;
    WCHAR szName[64];

    if (m_DelegatesLoaded)
        return;
    m_DelegatesLoaded = TRUE;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MyComputer\\NameSpace\\DelegateFolders",
                      0, KEY_ENUMERATE_SUB_KEYS, &hKey) != ERROR_SUCCESS)
    {
        return;
    }
    for (DWORD i = 0; m_DelegateCount < _countof(m_Delegates); ++i)
    {
        DWORD cchName = _countof(szName);
        if (RegEnumKeyExW(hKey, i, szName, &cchName, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        CLSID clsid;
        if (FAILED(CLSIDFromString(szName, &clsid)))
            continue;

        CComPtr<IShellFolder> pFolder;
        if (FAILED(SHCoCreateInstance(NULL, &clsid, NULL, IID_PPV_ARG(IShellFolder, &pFolder))))
            continue;

        CComPtr<IDelegateFolder> pDelegate;
        if (FAILED(pFolder->QueryInterface(IID_PPV_ARG(IDelegateFolder, &pDelegate))))
            continue;
        CDelegateMalloc *pMalloc = new CDelegateMalloc(clsid);
        if (!pMalloc)
            continue;
        HRESULT hr = pDelegate->SetItemAlloc(pMalloc);
        pMalloc->Release();
        if (FAILED(hr))
            continue;

        CComPtr<IPersistFolder> pPersist;
        if (SUCCEEDED(pFolder->QueryInterface(IID_PPV_ARG(IPersistFolder, &pPersist))))
            pPersist->Initialize(pidlRoot);

        m_Delegates[m_DelegateCount].clsid = clsid;
        m_Delegates[m_DelegateCount].folder = pFolder;
        m_DelegateCount++;
    }
    RegCloseKey(hKey);
}

IShellFolder *CDrivesFolder::FindDelegate(PCUIDLIST_RELATIVE pidl)
{
    CLSID clsid;

    if (!IsDelegateItem(pidl))
        return NULL;
    LoadDelegates();
    CopyMemory(&clsid, (const BYTE *)pidl + pidl->mkid.cb - sizeof(CLSID), sizeof(CLSID));
    for (UINT i = 0; i < m_DelegateCount; ++i)
    {
        if (IsEqualCLSID(m_Delegates[i].clsid, clsid))
            return m_Delegates[i].folder;
    }
    return NULL;
}

UINT CDrivesFolder::GetDelegateCount()
{
    LoadDelegates();
    return m_DelegateCount;
}

HRESULT CDrivesFolder::EnumDelegates(HWND hwndOwner, DWORD dwFlags, IEnumIDList **ppEnum, UINT Index)
{
    *ppEnum = NULL;
    LoadDelegates();
    if (Index >= m_DelegateCount)
        return E_INVALIDARG;
    return m_Delegates[Index].folder->EnumObjects(hwndOwner, dwFlags, ppEnum);
}

CDrivesFolder::~CDrivesFolder()
{
    TRACE("-- destroying IShellFolder(%p)\n", this);
    SHFree(pidlRoot);
}

HRESULT WINAPI CDrivesFolder::FinalConstruct()
{
    pidlRoot = _ILCreateMyComputer();    /* my qualified pidl */
    if (pidlRoot == NULL)
        return E_OUTOFMEMORY;

    REGFOLDERINITDATA RegInit = { static_cast<IShellFolder*>(this), &g_RegFolderInfo };
    HRESULT hr = CRegFolder_CreateInstance(&RegInit,
                                           pidlRoot,
                                           IID_PPV_ARG(IShellFolder2, &m_regFolder));

    return hr;
}

/**************************************************************************
*    CDrivesFolder::ParseDisplayName
*/
HRESULT WINAPI CDrivesFolder::ParseDisplayName(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName,
        DWORD * pchEaten, PIDLIST_RELATIVE * ppidl, DWORD * pdwAttributes)
{
    HRESULT hr = E_INVALIDARG;

    TRACE("(%p)->(HWND=%p,%p,%p=%s,%p,pidl=%p,%p)\n", this,
          hwndOwner, pbc, lpszDisplayName, debugstr_w (lpszDisplayName),
          pchEaten, ppidl, pdwAttributes);

    if (!ppidl)
        return hr;

    *ppidl = NULL;

    if (!lpszDisplayName)
        return hr;


    /* handle CLSID paths */
    if (lpszDisplayName[0] == L':' && lpszDisplayName[1] == L':')
    {
        return m_regFolder->ParseDisplayName(hwndOwner, pbc, lpszDisplayName, pchEaten, ppidl,
                                             pdwAttributes);
    }

    if (lpszDisplayName[0] == L'\\' && lpszDisplayName[1] == L'\\' &&
        lpszDisplayName[2] == L'?' && lpszDisplayName[3] == L'\\')
    {
        return E_INVALIDARG;
    }

    if (lpszDisplayName[0] == L'\\' && lpszDisplayName[1] == L'\\')
    {
        LoadDelegates();
        for (UINT i = 0; i < m_DelegateCount; ++i)
        {
            hr = m_Delegates[i].folder->ParseDisplayName(hwndOwner, pbc, lpszDisplayName, pchEaten, ppidl,
                                                         pdwAttributes);
            if (SUCCEEDED(hr))
                return hr;
        }
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    if (((L'A' <= lpszDisplayName[0] && lpszDisplayName[0] <= L'Z') ||
         (L'a' <= lpszDisplayName[0] && lpszDisplayName[0] <= L'z')) &&
        lpszDisplayName[1] == L':' && (lpszDisplayName[2] == L'\\' || !lpszDisplayName[2]))
    {
        // "C:\..."
        WCHAR szRoot[8];
        PathBuildRootW(szRoot, ((*lpszDisplayName - 1) & 0x1F));

        if (SHIsFileSysBindCtx(pbc, NULL) != S_OK && !(BindCtx_GetMode(pbc, 0) & STGM_CREATE))
        {
            if (::GetDriveType(szRoot) == DRIVE_NO_ROOT_DIR)
                return HRESULT_FROM_WIN32(ERROR_INVALID_DRIVE);
        }

        CComHeapPtr<ITEMIDLIST> pidlTemp(_ILCreateDrive(szRoot));
        if (!pidlTemp)
            return E_OUTOFMEMORY;

        if (lpszDisplayName[2] && lpszDisplayName[3])
        {
            CComPtr<IShellFolder> pChildFolder;
            hr = BindToObject(pidlTemp, pbc, IID_PPV_ARG(IShellFolder, &pChildFolder));
            if (FAILED_UNEXPECTEDLY(hr))
                return hr;

            ULONG chEaten;
            CComHeapPtr<ITEMIDLIST> pidlChild;
            hr = pChildFolder->ParseDisplayName(hwndOwner, pbc, &lpszDisplayName[3], &chEaten,
                                                &pidlChild, pdwAttributes);
            if (FAILED_UNEXPECTEDLY(hr))
                return hr;

            hr = SHILCombine(pidlTemp, pidlChild, ppidl);
        }
        else
        {
            *ppidl = pidlTemp.Detach();
            if (pdwAttributes && *pdwAttributes)
                GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)ppidl, pdwAttributes);
            hr = S_OK;
        }
    }

    TRACE("(%p)->(-- ret=0x%08x)\n", this, hr);

    return hr;
}

/**************************************************************************
*        CDrivesFolder::EnumObjects
*/
HRESULT WINAPI CDrivesFolder::EnumObjects(HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST *ppEnumIDList)
{
    CComPtr<IEnumIDList> pRegEnumerator;
    m_regFolder->EnumObjects(hwndOwner, dwFlags, &pRegEnumerator);

    return ShellObjectCreatorInit<CDrivesFolderEnum>(hwndOwner, dwFlags, pRegEnumerator, this, IID_PPV_ARG(IEnumIDList, ppEnumIDList));
}

/**************************************************************************
*        CDrivesFolder::BindToObject
*/
HRESULT WINAPI CDrivesFolder::BindToObject(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut)
{
    TRACE("(%p)->(pidl=%p,%p,%s,%p)\n", this,
          pidl, pbcReserved, shdebugstr_guid(&riid), ppvOut);

    if (!pidl)
        return E_INVALIDARG;


    if (const KnownFolderEntry *entry = GetKnownFolderChild(pidl))
    {
        if (!ppvOut)
            return E_POINTER;
        *ppvOut = NULL;
        /* LIBERNT_KNOWN_FOLDERS_V3: property interfaces are served by
         * the same filesystem property-store factory used for drives.
         * The shell-folder binding path remains unchanged. */
        PIDLIST_ABSOLUTE absolute = NULL;
        HRESULT hr = GetKnownFolderAbsolute(*entry->id, &absolute);
        if (FAILED(hr))
        {
            return hr;
        }

        if (IsEqualIID(riid, IID_IPropertyStoreFactory) ||
            IsEqualIID(riid, IID_IPropertyStore))
        {
            WCHAR path[MAX_PATH] = {0};
            if (!SHGetPathFromIDListW(absolute, path))
            {
                ILFree(absolute);
                return E_FAIL;
            }
            hr = CFSPropertyStoreFactory_CreateInstance(
                pidlRoot, this, pidl, path, riid, ppvOut);
        }
        else
        {
            hr = SHBindToObject(NULL, absolute, pbcReserved, riid, ppvOut);
        }
        ILFree(absolute);
        return hr;
    }

    if (IShellFolder *pDelegate = FindDelegate(pidl))
        return pDelegate->BindToObject(pidl, pbcReserved, riid, ppvOut);

    if (_ILIsSpecialFolder(pidl))
        return m_regFolder->BindToObject(pidl, pbcReserved, riid, ppvOut);

    /* LIBERNT_KNOWN_FOLDERS_V6_BEGIN */
    /* A narrow fallback for simple filesystem-folder PIDLs that arrive at
     * My Computer instead of their filesystem parent. Do not reinterpret
     * arbitrary PT_FOLDER items as drives. */
    if (ppvOut && pidl && pidl->mkid.cb >= 3 &&
        _ILIsPidlSimple(pidl) && pidl->mkid.abID[0] == PT_FOLDER)
    {
        /* Reuse the same physical-child classifier used by CompareIDs,
         * GetAttributesOf and GetDisplayNameOf. */
        if (const KnownFolderEntry *matched = V9KnownPhysicalChild(pidl))
        {
            PIDLIST_ABSOLUTE target = NULL;
            HRESULT v6hr = GetKnownFolderAbsolute(*matched->id, &target);
            if (SUCCEEDED(v6hr) && target)
            {
                CComPtr<IShellFolder> physicalParent;
                PCUITEMID_CHILD physicalChild = NULL;
                v6hr = SHBindToParent(target, IID_PPV_ARG(IShellFolder, &physicalParent),
                                      &physicalChild);
                if (SUCCEEDED(v6hr) && physicalParent && physicalChild)
                {
                    *ppvOut = NULL;
                    v6hr = physicalParent->BindToObject(physicalChild, pbcReserved,
                                                        riid, ppvOut);
                }
                ILFree(target);
                return v6hr;
            }
            if (target)
                ILFree(target);
            return FAILED(v6hr) ? v6hr : E_FAIL;
        }
    }
    /* LIBERNT_KNOWN_FOLDERS_V6_END */
    if (!_ILIsDrive(pidl))
    {
        return E_INVALIDARG;
    }

    CHAR* pchDrive = _ILGetDataPointer(pidl)->u.drive.szDriveName;

    PERSIST_FOLDER_TARGET_INFO pfti = {0};
    pfti.dwAttributes = -1;
    pfti.csidl = -1;
    pfti.szTargetParsingName[0] = *pchDrive;
    pfti.szTargetParsingName[1] = L':';
    pfti.szTargetParsingName[2] = L'\\';

    if (_ILIsPidlSimple(pidl) && (IsEqualIID(riid, IID_IPropertyStoreFactory) || IsEqualIID(riid, IID_IPropertyStore)))
        return CFSPropertyStoreFactory_CreateInstance(pidlRoot, this, pidl, pfti.szTargetParsingName, riid, ppvOut);

    HRESULT hr = SHELL32_BindToSF(pidlRoot,
                                  &pfti,
                                  pidl,
                                  &CLSID_ShellFSFolder,
                                  riid,
                                  ppvOut);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return S_OK;
}

/**************************************************************************
*    CDrivesFolder::BindToStorage
*/
HRESULT WINAPI CDrivesFolder::BindToStorage(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut)
{
    FIXME("(%p)->(pidl=%p,%p,%s,%p) stub\n", this,
          pidl, pbcReserved, shdebugstr_guid (&riid), ppvOut);

    *ppvOut = NULL;
    return E_NOTIMPL;
}

/**************************************************************************
*     CDrivesFolder::CompareIDs
*/
HRESULT WINAPI CDrivesFolder::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    HRESULT hres;
    UINT iColumn = LOWORD(lParam);

    if (iColumn >= _countof(MyComputerSFHeader))
        return E_INVALIDARG;

    if (!pidl1 || !pidl2)
    {
        ERR("Got null pidl pointer (%Ix %p %p)!\n", lParam, pidl1, pidl2);
        return E_INVALIDARG;
    }

    /* Group all six known folders ahead of drives, in the same stable order. */
    const KnownFolderEntry *known1 = GetKnownFolderChild(pidl1);
    const KnownFolderEntry *known2 = GetKnownFolderChild(pidl2);
    if (known1 || known2)
    {
        if (!known1 || !known2)
            return MAKE_COMPARE_HRESULT(known1 ? -1 : 1);
        INT index1 = 0, index2 = 0;
        for (UINT i = 0; i < _countof(s_KnownFolders); ++i)
        {
            if (known1 == &s_KnownFolders[i]) index1 = i;
            if (known2 == &s_KnownFolders[i]) index2 = i;
        }
        return MAKE_COMPARE_HRESULT(index1 - index2);
    }

    const INT type1 = IsFileSystemFolderItem(pidl1) ? 0 : IsSimpleDriveItem(pidl1) ? 1 : IsRegItem(pidl1) ? 2 :
                      IsDelegateItem(pidl1) ? 3 : -1;
    const INT type2 = IsFileSystemFolderItem(pidl2) ? 0 : IsSimpleDriveItem(pidl2) ? 1 : IsRegItem(pidl2) ? 2 :
                      IsDelegateItem(pidl2) ? 3 : -1;
    /* LIBERNT_KNOWN_FOLDERS_V9_COMPARE */
    const KnownFolderEntry *v9_1 = V9KnownPhysicalChild(pidl1);
    const KnownFolderEntry *v9_2 = V9KnownPhysicalChild(pidl2);
    if (v9_1 || v9_2)
    {
        /* Do not read a PT_FOLDER as the drive structure below. */
        if (v9_1 && v9_2)
            return MAKE_COMPARE_HRESULT(lstrcmpiW(v9_1->name, v9_2->name));
        return MAKE_COMPARE_HRESULT(v9_1 ? -1 : 1);
    }
    if (type1 < 0 && FAILED_UNEXPECTEDLY(E_INVALIDARG))
        return E_INVALIDARG;
    if (type2 < 0 && FAILED_UNEXPECTEDLY(E_INVALIDARG))
        return E_INVALIDARG;
    if (type1 != type2)
        return MAKE_COMPARE_HRESULT(type1 - type2);
    if (type1 == 3)
    {
        IShellFolder *pDelegate1 = FindDelegate(pidl1), *pDelegate2 = FindDelegate(pidl2);
        if (!pDelegate1 || pDelegate1 != pDelegate2)
            return MAKE_COMPARE_HRESULT(memcmp((const BYTE *)pidl1 + pidl1->mkid.cb - sizeof(CLSID),
                                               (const BYTE *)pidl2 + pidl2->mkid.cb - sizeof(CLSID), sizeof(CLSID)));
        return pDelegate1->CompareIDs(lParam, pidl1, pidl2);
    }
    if (type1 == 0)
    {
        int result = _stricmp(_ILGetDataPointer(pidl1)->u.drive.szDriveName, _ILGetDataPointer(pidl2)->u.drive.szDriveName);
        if (result)
            return MAKE_COMPARE_HRESULT(result);
    }
    if (type1 != 1)
        return SHELL32_CompareDetails(this, lParam, pidl1, pidl2);

    CHAR* pszDrive1 = _ILGetDataPointer(pidl1)->u.drive.szDriveName;
    CHAR* pszDrive2 = _ILGetDataPointer(pidl2)->u.drive.szDriveName;

    int result;
    switch (MyComputerSFHeader[iColumn].colnameid)
    {
        case IDS_SHV_COLUMN_NAME:
        {
            result = _stricmp(pszDrive1, pszDrive2);
            hres = MAKE_COMPARE_HRESULT(result);
            break;
        }

        case IDS_SHV_COLUMN_DISK_CAPACITY:
        case IDS_SHV_COLUMN_DISK_AVAILABLE:
        {
            ULARGE_INTEGER Drive1Available, Drive1Total, Drive2Available, Drive2Total;
            BOOL bValid1 = FALSE, bValid2 = FALSE;

            if (GetVolumeInformationA(pszDrive1, NULL, 0, NULL, NULL, NULL, NULL, 0))
                bValid1 = GetDiskFreeSpaceExA(pszDrive1, &Drive1Available, &Drive1Total, NULL);
            else
                Drive1Available.QuadPart = Drive1Total.QuadPart = 0;

            if (GetVolumeInformationA(pszDrive2, NULL, 0, NULL, NULL, NULL, NULL, 0))
                bValid2 = GetDiskFreeSpaceExA(pszDrive2, &Drive2Available, &Drive2Total, NULL);
            else
                Drive2Available.QuadPart = Drive2Total.QuadPart = 0;

            LARGE_INTEGER Diff;
            if (MyComputerSFHeader[iColumn].colnameid == IDS_SHV_COLUMN_DISK_CAPACITY) /* Size */
                Diff.QuadPart = Drive1Total.QuadPart - Drive2Total.QuadPart;
            else /* Size available */
                Diff.QuadPart = Drive1Available.QuadPart - Drive2Available.QuadPart;

            if (bValid1 != bValid2)
                hres = MAKE_COMPARE_HRESULT((int)!bValid1 - (int)!bValid2);
            else
                hres = MAKE_COMPARE_HRESULT(Diff.QuadPart);

            break;
        }
        case IDS_SHV_COLUMN_TYPE:
        case IDS_SHV_COLUMN_COMMENTS:
        case IDS_SHV_COLUMN_FILESYSTEM:
        case IDS_SHV_COLUMN_NETLOCATION:
        case IDS_SHV_COLUMN_PERCENTFULL:
        case IDS_SHV_COLUMN_FREESPACE:
        {
            /* We want to return immediately because SHELL32_CompareDetails also compares children. */
            return SHELL32_CompareDetails(this, lParam, pidl1, pidl2);
        }
        DEFAULT_UNREACHABLE;
    }

    if (HRESULT_CODE(hres) == 0)
        return SHELL32_CompareChildren(this, lParam, pidl1, pidl2);

    return hres;
}

/**************************************************************************
*    CDrivesFolder::CreateViewObject
*/
class CDrivesFolderDropTarget :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDropTarget
{
public:
    STDMETHOD(DragEnter)(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override
    {
        if (!pdwEffect)
            return E_INVALIDARG;
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override
    {
        if (!pdwEffect)
            return E_INVALIDARG;
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    STDMETHOD(DragLeave)() override
    {
        return S_OK;
    }

    STDMETHOD(Drop)(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override
    {
        if (!pdwEffect)
            return E_INVALIDARG;
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

DECLARE_NOT_AGGREGATABLE(CDrivesFolderDropTarget)
DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CDrivesFolderDropTarget)
    COM_INTERFACE_ENTRY_IID(IID_IDropTarget, IDropTarget)
END_COM_MAP()
};

HRESULT WINAPI CDrivesFolder::CreateViewObject(HWND hwndOwner, REFIID riid, LPVOID * ppvOut)
{
    CComPtr<IShellView> pShellView;
    HRESULT hr = E_INVALIDARG;

    TRACE("(%p)->(hwnd=%p,%s,%p)\n", this,
          hwndOwner, shdebugstr_guid (&riid), ppvOut);

    if (!ppvOut)
        return hr;

    *ppvOut = NULL;

    if (IsEqualIID(riid, IID_IDropTarget))
    {
        hr = ShellObjectCreator<CDrivesFolderDropTarget>(riid, ppvOut);
    }
    else if (IsEqualIID(riid, IID_IContextMenu))
    {
        DEFCONTEXTMENU dcm = { hwndOwner, this, pidlRoot, this };
        hr = SHCreateDefaultContextMenu(&dcm, riid, ppvOut);
    }
    else if (IsEqualIID(riid, IID_IShellView))
    {
        CComPtr<CDrivesFolderViewCB> sfviewcb;
        if (SUCCEEDED(hr = ShellObjectCreator(sfviewcb)))
        {
            sfviewcb->Initialize(this);
            SFV_CREATE sfvparams = { sizeof(SFV_CREATE), this, NULL, sfviewcb };
            hr = SHCreateShellFolderView(&sfvparams, (IShellView**)ppvOut);
        }
    }
    TRACE("-- (%p)->(interface=%p)\n", this, ppvOut);
    return hr;
}

/**************************************************************************
*  CDrivesFolder::GetAttributesOf
*/
HRESULT WINAPI CDrivesFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD * rgfInOut)
{
    TRACE("(%p)->(cidl=%d apidl=%p mask=%p (0x%08x))\n",
          this, cidl, apidl, rgfInOut, rgfInOut ? *rgfInOut : 0);

    if (cidl && !apidl)
        return E_INVALIDARG;

    for (UINT i = 0; i < cidl; ++i)
    {
        if (!apidl[i] || _ILIsEmpty(apidl[i]))
            return E_INVALIDARG;
    }

    if(cidl == 0)
        *rgfInOut &= dwComputerAttributes;
    else
    {
        for (UINT i = 0; i < cidl; ++i)
        {
            /* LIBERNT_KNOWN_FOLDERS_V9_ATTRIBUTES */
            if (V9KnownPhysicalChild(apidl[i]))
            {
                *rgfInOut &= (SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_HASSUBFOLDER |
                               SFGAO_BROWSABLE | SFGAO_DROPTARGET | SFGAO_CANCOPY);
            }
            else if (GetKnownFolderChild(apidl[i]))
            {
                *rgfInOut &= (SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_HASSUBFOLDER |
                               SFGAO_BROWSABLE | SFGAO_DROPTARGET | SFGAO_CANCOPY);
            }
            else if (IShellFolder *pDelegate = FindDelegate(apidl[i]))
            {
                if (FAILED(pDelegate->GetAttributesOf(1, &apidl[i], rgfInOut)))
                    *rgfInOut = 0;
            }
            else if (IsFileSystemFolderItem(apidl[i]))
            {
                CComHeapPtr<ITEMIDLIST> absolute(ILCombine(pidlRoot, apidl[i]));
                CComPtr<IShellFolder> parent;
                PCUITEMID_CHILD child;
                HRESULT hr = absolute ? SHBindToParent(absolute, IID_PPV_ARG(IShellFolder, &parent), &child) : E_OUTOFMEMORY;
                if (SUCCEEDED(hr))
                    hr = parent->GetAttributesOf(1, &child, rgfInOut);
                if (FAILED(hr))
                    *rgfInOut = 0;
                *rgfInOut &= ~(SFGAO_CANDELETE | SFGAO_CANRENAME);
            }
            else if (IsSimpleDriveItem(apidl[i]))
            {
                *rgfInOut &= dwDriveAttributes;

                if (_ILGetDriveType(apidl[i]) == DRIVE_CDROM)
                    *rgfInOut &= ~SFGAO_CANRENAME; // CD-ROM drive cannot rename
            }
            else if (IsRegItem(apidl[i], CLSID_ControlPanel))
            {
                *rgfInOut &= dwControlPanelAttributes;
            }
            else if (_ILIsSpecialFolder(apidl[i]))
            {
                m_regFolder->GetAttributesOf(1, &apidl[i], rgfInOut);
            }
            else
            {
                ERR("Got unknown pidl type!\n");
            }
        }
    }

    /* make sure SFGAO_VALIDATE is cleared, some apps depend on that */
    *rgfInOut &= ~SFGAO_VALIDATE;

    TRACE("-- result=0x%08x\n", *rgfInOut);
    return S_OK;
}

/**************************************************************************
*    CDrivesFolder::GetUIObjectOf
*
* PARAMETERS
*  hwndOwner [in]  Parent window for any output
*  cidl      [in]  array size
*  apidl     [in]  simple pidl array
*  riid      [in]  Requested Interface
*  prgfInOut [   ] reserved
*  ppvObject [out] Resulting Interface
*
*/
HRESULT WINAPI CDrivesFolder::GetUIObjectOf(HWND hwndOwner,
    UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
    REFIID riid, UINT *prgfInOut, LPVOID *ppvOut)
{
    LPVOID pObj = NULL;
    HRESULT hr = E_INVALIDARG;

    TRACE("(%p)->(%p,%u,apidl=%p,%s,%p,%p)\n", this,
          hwndOwner, cidl, apidl, shdebugstr_guid (&riid), prgfInOut, ppvOut);

    if (!ppvOut)
        return hr;

    *ppvOut = NULL;

    if (cidl == 1 && apidl)
    {
        if (const KnownFolderEntry *entry = GetKnownFolderChild(apidl[0]))
            return KnownFolderUIObject(hwndOwner, *entry->id, riid, prgfInOut, ppvOut);
    }

    if (cidl >= 1 && apidl)
    {
        if (IShellFolder *pDelegate = FindDelegate(apidl[0]))
            return pDelegate->GetUIObjectOf(hwndOwner, cidl, apidl, riid, prgfInOut, ppvOut);
    }

    if (IsEqualIID (riid, IID_IContextMenu) && (cidl >= 1))
    {
        if (cidl == 1 && IsFileSystemFolderItem(apidl[0]))
            hr = GetFileSystemFolderUIObject(pidlRoot, hwndOwner, apidl[0], riid, prgfInOut, &pObj);
        else if (IsSimpleDriveItem(apidl[0]))
            hr = CDrivesContextMenu_CreateInstance(pidlRoot, hwndOwner, cidl, apidl, static_cast<IShellFolder*>(this), (IContextMenu**)&pObj);
        else
            hr = m_regFolder->GetUIObjectOf(hwndOwner, cidl, apidl, riid, prgfInOut, &pObj);
    }
    else if (IsEqualIID (riid, IID_IDataObject) && (cidl >= 1))
    {
        if (cidl == 1 && IsFileSystemFolderItem(apidl[0]))
            hr = GetFileSystemFolderUIObject(pidlRoot, hwndOwner, apidl[0], riid, prgfInOut, &pObj);
        else
            hr = IDataObject_Constructor(hwndOwner, pidlRoot, apidl, cidl, TRUE, (IDataObject **)&pObj);
    }
    else if ((IsEqualIID (riid, IID_IExtractIconA) || IsEqualIID (riid, IID_IExtractIconW)) && (cidl == 1))
    {
        if (IsFileSystemFolderItem(apidl[0]))
            hr = GetFileSystemFolderUIObject(pidlRoot, hwndOwner, apidl[0], riid, prgfInOut, &pObj);
        else if (IsSimpleDriveItem(apidl[0]))
            hr = CDrivesExtractIcon_CreateInstance(this, apidl[0], riid, &pObj);
        else
            hr = m_regFolder->GetUIObjectOf(hwndOwner, cidl, apidl, riid, prgfInOut, &pObj);
    }
    else if (IsEqualIID (riid, IID_IDropTarget) && (cidl == 1))
    {
        CComPtr<IShellFolder> psfChild;
        hr = this->BindToObject(apidl[0], NULL, IID_PPV_ARG(IShellFolder, &psfChild));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        return psfChild->CreateViewObject(NULL, riid, ppvOut);
    }
    else
        hr = E_NOINTERFACE;

    if (SUCCEEDED(hr) && !pObj)
        hr = E_OUTOFMEMORY;

    *ppvOut = pObj;
    TRACE("(%p)->hr=0x%08x\n", this, hr);
    return hr;
}

/**************************************************************************
*    CDrivesFolder::GetDisplayNameOf
*/
HRESULT WINAPI CDrivesFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, DWORD dwFlags, LPSTRRET strRet)
{
    WCHAR szDrive[8];

    TRACE("(%p)->(pidl=%p,0x%08x,%p)\n", this, pidl, dwFlags, strRet);
    pdump (pidl);


    if (!strRet)
        return E_INVALIDARG;

    /* LIBERNT_KNOWN_FOLDERS_V9_DISPLAY */
    if (const KnownFolderEntry *v9_entry = V9KnownPhysicalChild(pidl))
    {
        if (dwFlags & SHGDN_FORPARSING)
        {
            PIDLIST_ABSOLUTE v9_abs = NULL;
            HRESULT v9_hr = GetKnownFolderAbsolute(*v9_entry->id, &v9_abs);
            if (FAILED(v9_hr)) return v9_hr;
            WCHAR v9_path[MAX_PATH] = {0};
            BOOL v9_ok = SHGetPathFromIDListW(v9_abs, v9_path);
            ILFree(v9_abs);
            return v9_ok ? SHSetStrRet(strRet, v9_path) : E_FAIL;
        }
        return SHSetStrRet(strRet, v9_entry->name);
    }
    if (const KnownFolderEntry *entry = GetKnownFolderChild(pidl))
    {
        if (dwFlags & SHGDN_FORPARSING)
        {
            PIDLIST_ABSOLUTE absolute = NULL;
            HRESULT hr = GetKnownFolderAbsolute(*entry->id, &absolute);
            if (FAILED(hr))
                return hr;
            WCHAR path[MAX_PATH];
            BOOL ok = SHGetPathFromIDListW(absolute, path);
            ILFree(absolute);
            return ok ? SHSetStrRet(strRet, path) : E_FAIL;
        }
        return SHSetStrRet(strRet, entry->name);
    }

    if (IShellFolder *pDelegate = FindDelegate(pidl))
    {
        return pDelegate->GetDisplayNameOf(pidl, dwFlags, strRet);
    }
    else if (!_ILIsPidlSimple (pidl))
    {
        return SHELL32_GetDisplayNameOfChild(this, pidl, dwFlags, strRet);
    }
    else if (_ILIsSpecialFolder(pidl))
    {
        return m_regFolder->GetDisplayNameOf(pidl, dwFlags, strRet);
    }
    else if (GetDrivePath(pidl, szDrive) < 0)
    {
        ERR("Wrong pidl type\n");
        return E_INVALIDARG;
    }

    PWSTR pszPath = (LPWSTR)CoTaskMemAlloc((MAX_PATH + 1) * sizeof(WCHAR));
    if (!pszPath)
        return E_OUTOFMEMORY;
    pszPath[0] = UNICODE_NULL;
    szDrive[0] &= ~32; // Always uppercase

    /* long view "lw_name (C:)" */
    BOOL bEditLabel = GET_SHGDN_RELATION(dwFlags) == SHGDN_INFOLDER && (dwFlags & SHGDN_FOREDITING);
    if (!(dwFlags & SHGDN_FORPARSING))
    {
        if (m_DriveDisplayMode < 0)
        {
            DWORD err, type, data, cb = sizeof(data);
            err = SHRegGetUSValueW(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer",
                                   L"ShowDriveLettersFirst", &type, &data, &cb, FALSE, NULL, 0);
            m_DriveDisplayMode = (!err && type == REG_DWORD && cb == sizeof(data)) ? (BYTE)data : 0;
        }
        BOOL bRemoteFirst = m_DriveDisplayMode == 1;
        BOOL bNoLetter = m_DriveDisplayMode == 2;
        BOOL bAllFirst = m_DriveDisplayMode == 4;
        PWSTR pszLabel = pszPath;

        if (!bNoLetter && (bAllFirst || (bRemoteFirst && GetCachedDriveType(szDrive) == DRIVE_REMOTE)))
        {
            bNoLetter = TRUE; // Handling the letter now, don't append it again later
            if (!bEditLabel)
                pszLabel += wsprintfW(pszPath, L"(%c:) ", szDrive[0]);
        }

        if (GetDriveLabel(szDrive, pszLabel, MAX_PATH - 7) != S_OK && !bEditLabel)
        {
            UINT ResId = 0, DrvType = GetCachedDriveType(szDrive);
            if (DrvType == DRIVE_REMOVABLE)
                ResId = IsFloppyDrive(szDrive) ? IDS_DRIVE_FLOPPY : IDS_DRIVE_REMOVABLE;
            else if (DrvType < _countof(iDriveTypeIds))
                ResId = iDriveTypeIds[DrvType];

            if (ResId)
            {
                UINT len = LoadStringW(shell32_hInstance, ResId, pszLabel, MAX_PATH - 7);
                if (len > MAX_PATH - 7)
                    pszLabel[MAX_PATH-7] = UNICODE_NULL;
            }
        }

        if (!*pszLabel && !bEditLabel) // No label nor fallback description, use SHGDN_FORPARSING
            *pszPath = UNICODE_NULL;
        else if (!bNoLetter && !bEditLabel)
            wsprintfW(pszPath + wcslen(pszPath), L" (%c:)", szDrive[0]);
    }

    if (!*pszPath && !bEditLabel) // SHGDN_FORPARSING or failure above (except editing empty label)
    {
        if (GET_SHGDN_RELATION(dwFlags) == SHGDN_INFOLDER)
            szDrive[2] = UNICODE_NULL; // Remove backslash
        wcscpy(pszPath, szDrive);
    }
    strRet->uType = STRRET_WSTR;
    strRet->pOleStr = pszPath;

    TRACE("-- (%p)->(%s)\n", this, strRet->uType == STRRET_CSTR ? strRet->cStr : debugstr_w(strRet->pOleStr));
    return S_OK;
}

/**************************************************************************
*  CDrivesFolder::SetNameOf
*  Changes the name of a file object or subfolder, possibly changing its item
*  identifier in the process.
*
* PARAMETERS
*  hwndOwner  [in]   Owner window for output
*  pidl       [in]   simple pidl of item to change
*  lpszName   [in]   the items new display name
*  dwFlags    [in]   SHGNO formatting flags
*  ppidlOut   [out]  simple pidl returned
*/
HRESULT WINAPI CDrivesFolder::SetNameOf(HWND hwndOwner, PCUITEMID_CHILD pidl,
                                        LPCOLESTR lpName, DWORD dwFlags, PITEMID_CHILD *pPidlOut)
{
    if (IShellFolder *pDelegate = FindDelegate(pidl))
        return pDelegate->SetNameOf(hwndOwner, pidl, lpName, dwFlags, pPidlOut);
    if (IsSimpleDriveItem(pidl))
    {
        WCHAR szDrive[8];
        HRESULT hr = GetDrivePath(pidl, szDrive) >= 0 ? SetDriveLabel(hwndOwner, szDrive, lpName) : E_FAIL;
        if (pPidlOut)
            *pPidlOut = SUCCEEDED(hr) ? _ILCreateDrive(szDrive) : NULL;
        return hr;
    }
    if (IsFileSystemFolderItem(pidl))
        return E_ACCESSDENIED;
    return m_regFolder->SetNameOf(hwndOwner, pidl, lpName, dwFlags, pPidlOut);
}

HRESULT WINAPI CDrivesFolder::GetDefaultSearchGUID(GUID * pguid)
{
    FIXME("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CDrivesFolder::EnumSearches(IEnumExtraSearch ** ppenum)
{
    FIXME("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CDrivesFolder::GetDefaultColumn (DWORD dwRes, ULONG *pSort, ULONG *pDisplay)
{
    TRACE("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CDrivesFolder::GetDefaultColumnState(UINT iColumn, SHCOLSTATEF * pcsFlags)
{
    TRACE("(%p)\n", this);

    if (!pcsFlags || iColumn >= _countof(MyComputerSFHeader))
        return E_INVALIDARG;
    *pcsFlags = MyComputerSFHeader[iColumn].colstate;
    return S_OK;
}

HRESULT WINAPI CDrivesFolder::GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv)
{
    const CLSID *pCLSID = IsRegItem(pidl);
    if (IShellFolder *pDelegate = FindDelegate(pidl))
    {
        CComPtr<IShellFolder2> pDelegate2;
        HRESULT hr = pDelegate->QueryInterface(IID_PPV_ARG(IShellFolder2, &pDelegate2));
        if (SUCCEEDED(hr))
            hr = pDelegate2->GetDetailsEx(pidl, pscid, pv);
        if (FAILED(hr) && pscid->fmtid == FMTID_ShellDetails && pscid->pid == PID_DESCRIPTIONID)
            return SHELL_CreateSHDESCRIPTIONID(pv, SHDID_COMPUTER_OTHER, &CLSID_NULL);
        return hr;
    }
    if (IsFileSystemFolderItem(pidl))
        return SH32_GetDetailsOfPKeyAsVariant(this, pidl, pscid, pv, FALSE);
    if (pscid->fmtid == FMTID_ShellDetails)
    {
        switch (pscid->pid)
        {
            case PID_DESCRIPTIONID:
            {
                if (pCLSID)
                    return SHELL_CreateSHDESCRIPTIONID(pv, SHDID_ROOT_REGITEM, pCLSID);
                UINT id = SHDID_COMPUTER_OTHER;
                switch (_ILGetDriveType(pidl))
                {
                    case DRIVE_REMOVABLE: id = _ILGetRemovableTypeId(pidl); break;
                    case DRIVE_FIXED:     id = SHDID_COMPUTER_FIXED; break;
                    case DRIVE_REMOTE:    id = SHDID_COMPUTER_NETDRIVE; break;
                    case DRIVE_CDROM:     id = SHDID_COMPUTER_CDROM; break;
                    case DRIVE_RAMDISK:   id = SHDID_COMPUTER_RAMDISK; break;
                }
                return SHELL_CreateSHDESCRIPTIONID(pv, id, &CLSID_NULL);
            }
        }
    }
    if (pCLSID)
        return m_regFolder->GetDetailsEx(pidl, pscid, pv);
    return SH32_GetDetailsOfPKeyAsVariant(this, pidl, pscid, pv, FALSE);
}

HRESULT WINAPI CDrivesFolder::GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd)
{
    HRESULT hr;

    TRACE("(%p)->(%p %i %p)\n", this, pidl, iColumn, psd);

    if (!psd)
        return E_INVALIDARG;
    if (iColumn >= _countof(MyComputerSFHeader))
        return pidl ? E_INVALIDARG : E_NOTIMPL;

    if (!pidl)
    {
        psd->fmt = MyComputerSFHeader[iColumn].fmt;
        psd->cxChar = MyComputerSFHeader[iColumn].cxChar;
        return SHSetStrRet(&psd->str, MyComputerSFHeader[iColumn].colnameid);
    }
    else if (IShellFolder *pDelegate = FindDelegate(pidl))
    {
        CComPtr<IShellFolder2> pDelegate2;
        hr = pDelegate->QueryInterface(IID_PPV_ARG(IShellFolder2, &pDelegate2));
        if (SUCCEEDED(hr))
            hr = pDelegate2->GetDetailsOf(pidl, iColumn, psd);
        return hr;
    }
    else if (GetKnownFolderChild(pidl))
    {
        if (MyComputerSFHeader[iColumn].colnameid == IDS_SHV_COLUMN_NAME)
            return GetDisplayNameOf(pidl, SHGDN_NORMAL | SHGDN_INFOLDER, &psd->str);
        return SHSetStrRetEmpty(&psd->str);
    }
    else if (IsFileSystemFolderItem(pidl))
    {
        switch (MyComputerSFHeader[iColumn].colnameid)
        {
            case IDS_SHV_COLUMN_NAME:
                return GetDisplayNameOf(pidl, SHGDN_NORMAL | SHGDN_INFOLDER, &psd->str);
            default:
                return SHSetStrRetEmpty(&psd->str);
        }
    }
    else if (!IsSimpleDriveItem(pidl))
    {
        switch (MyComputerSFHeader[iColumn].colnameid)
        {
            case IDS_SHV_COLUMN_NAME:
                return m_regFolder->GetDetailsOf(pidl, SHFSF_COL_NAME, psd);
            case IDS_SHV_COLUMN_TYPE:
                return m_regFolder->GetDetailsOf(pidl, SHFSF_COL_TYPE, psd);
            case IDS_SHV_COLUMN_COMMENTS:
                return m_regFolder->GetDetailsOf(pidl, SHFSF_COL_COMMENT, psd);
            default:
                return SHSetStrRetEmpty(&psd->str);
        }
    }
    else
    {
        ULARGE_INTEGER ulTotalBytes, ulFreeBytes;
        WCHAR szDrive[8];
        INT8 DriveNum = GetDrivePath(pidl, szDrive);
        UINT DriveType = GetCachedDriveType(DriveNum);
        if (DriveType > DRIVE_RAMDISK)
            DriveType = DRIVE_FIXED;

        switch (MyComputerSFHeader[iColumn].colnameid)
        {
            case IDS_SHV_COLUMN_NAME:
                hr = GetDisplayNameOf(pidl, SHGDN_NORMAL | SHGDN_INFOLDER, &psd->str);
                break;
            case IDS_SHV_COLUMN_TYPE:
                if (DriveType == DRIVE_UNKNOWN)
                    hr = SHSetStrRetEmpty(&psd->str);
                else if (DriveType == DRIVE_REMOVABLE && IsFloppyDrive(szDrive))
                    hr = SHSetStrRet(&psd->str, IDS_DRIVE_FLOPPY);
                else
                    hr = SHSetStrRet(&psd->str, iDriveTypeIds[DriveType]);
                break;
            case IDS_SHV_COLUMN_DISK_CAPACITY:
            case IDS_SHV_COLUMN_DISK_AVAILABLE:
                psd->str.cStr[0] = 0x00;
                psd->str.uType = STRRET_CSTR;
                if (GetVolumeInformationW(szDrive, NULL, 0, NULL, NULL, NULL, NULL, 0))
                {
                    GetDiskFreeSpaceExW(szDrive, &ulFreeBytes, &ulTotalBytes, NULL);
                    if (MyComputerSFHeader[iColumn].colnameid == IDS_SHV_COLUMN_DISK_CAPACITY)
                        StrFormatByteSize64A(ulTotalBytes.QuadPart, psd->str.cStr, MAX_PATH);
                    else
                        StrFormatByteSize64A(ulFreeBytes.QuadPart, psd->str.cStr, MAX_PATH);
                }
                hr = S_OK;
                break;
            case IDS_SHV_COLUMN_FILESYSTEM:
            {
                WCHAR szFileSystem[MAX_PATH];
                if (GetVolumeInformationW(szDrive, NULL, 0, NULL, NULL, NULL, szFileSystem, _countof(szFileSystem)))
                    hr = SHSetStrRet(&psd->str, szFileSystem);
                else
                    hr = SHSetStrRetEmpty(&psd->str);
                break;
            }
            case IDS_SHV_COLUMN_COMMENTS:
                hr = E_FAIL;
                break;
            case IDS_SHV_COLUMN_NETLOCATION:
                hr = SHSetStrRetEmpty(&psd->str);
                break;
            case IDS_SHV_COLUMN_PERCENTFULL:
            case IDS_SHV_COLUMN_FREESPACE:
            {
                WCHAR szText[MAX_PATH], szFree[64], szTotal[64], szFormat[64];
                szText[0] = UNICODE_NULL;
                if (GetVolumeInformationW(szDrive, NULL, 0, NULL, NULL, NULL, NULL, 0) &&
                    GetDiskFreeSpaceExW(szDrive, &ulFreeBytes, &ulTotalBytes, NULL) && ulTotalBytes.QuadPart)
                {
                    if (MyComputerSFHeader[iColumn].colnameid == IDS_SHV_COLUMN_PERCENTFULL)
                    {
                        ULONGLONG Used = ulTotalBytes.QuadPart - min(ulFreeBytes.QuadPart, ulTotalBytes.QuadPart);
                        StringCchPrintfW(szText, _countof(szText), L"\x200E%u%%",
                                         (UINT)((Used * 100 + ulTotalBytes.QuadPart / 2) / ulTotalBytes.QuadPart));
                    }
                    else if (LoadStringW(shell32_hInstance, IDS_COMPUTER_FREE_SPACE, szFormat, _countof(szFormat)))
                    {
                        StrFormatByteSizeW(ulFreeBytes.QuadPart, szFree, _countof(szFree));
                        StrFormatByteSizeW(ulTotalBytes.QuadPart, szTotal, _countof(szTotal));
                        szText[0] = 0x200E;
                        StringCchPrintfW(szText + 1, _countof(szText) - 1, szFormat, szFree, szTotal);
                    }
                }
                hr = SHSetStrRet(&psd->str, szText);
                break;
            }
            DEFAULT_UNREACHABLE;
        }
    }

    return hr;
}

HRESULT WINAPI CDrivesFolder::MapColumnToSCID(UINT column, SHCOLUMNID *pscid)
{
    switch (column < _countof(MyComputerSFHeader) ? MyComputerSFHeader[column].colnameid : ~0UL)
    {
        case IDS_SHV_COLUMN_NAME: return MakeSCID(*pscid, FMTID_Storage, PID_STG_NAME);
        case IDS_SHV_COLUMN_TYPE: return MakeSCID(*pscid, FMTID_Storage, PID_STG_STORAGETYPE);
        case IDS_SHV_COLUMN_DISK_CAPACITY: return MakeSCID(*pscid, PKEY_Capacity.fmtid, PKEY_Capacity.pid);
        case IDS_SHV_COLUMN_DISK_AVAILABLE: return MakeSCID(*pscid, PKEY_FreeSpace.fmtid, PKEY_FreeSpace.pid);
        case IDS_SHV_COLUMN_FILESYSTEM: return MakeSCID(*pscid, PKEY_Volume_FileSystem.fmtid, PKEY_Volume_FileSystem.pid);
        case IDS_SHV_COLUMN_COMMENTS: return MakeSCID(*pscid, FMTID_SummaryInformation, PIDSI_COMMENTS);
        case IDS_SHV_COLUMN_NETLOCATION: *pscid = MyComputerNetworkLocationScid; return S_OK;
        case IDS_SHV_COLUMN_PERCENTFULL: return MakeSCID(*pscid, PKEY_PercentFull.fmtid, PKEY_PercentFull.pid);
        case IDS_SHV_COLUMN_FREESPACE: return MakeSCID(*pscid, PKEY_Computer_DecoratedFreeSpace.fmtid, PKEY_Computer_DecoratedFreeSpace.pid);
    }
    return E_INVALIDARG;
}

/************************************************************************
 *    CDrivesFolder::GetClassID
 */
HRESULT WINAPI CDrivesFolder::GetClassID(CLSID *lpClassId)
{
    TRACE("(%p)\n", this);

    if (!lpClassId)
        return E_POINTER;

    *lpClassId = CLSID_MyComputer;
    return S_OK;
}

/************************************************************************
 *    CDrivesFolder::Initialize
 *
 * NOTES: it makes no sense to change the pidl
 */
HRESULT WINAPI CDrivesFolder::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    return S_OK;
}

/**************************************************************************
 *    CDrivesFolder::GetCurFolder
 */
HRESULT WINAPI CDrivesFolder::GetCurFolder(PIDLIST_ABSOLUTE *pidl)
{
    TRACE("(%p)->(%p)\n", this, pidl);

    if (!pidl)
        return E_INVALIDARG; /* xp doesn't have this check and crashes on NULL */

    *pidl = ILClone(pidlRoot);
    return S_OK;
}

BOOL CDrivesFolder::ShouldEnumDrive(PCWSTR DrivePath)
{
    if (!m_bHideNonRemovableDrives || m_bForceAllDrivesRemovable)
        return TRUE;
    return GetDriveTypeW(DrivePath) == DRIVE_REMOVABLE;
}

HRESULT WINAPI CDrivesFolder::GetEnumFlags(IShellFolder *psf, PCIDLIST_ABSOLUTE pidlFolder, HWND *phwnd, DWORD *pgrfFlags)
{
    if (phwnd)
        *phwnd = NULL;
    if (pgrfFlags)
        *pgrfFlags = 0;
    return E_NOTIMPL;
}

HRESULT WINAPI CDrivesFolder::InitNew()
{
    return S_OK;
}

HRESULT WINAPI CDrivesFolder::Load(IPropertyBag *pPropBag, IErrorLog *pErrorLog)
{
    BOOL bValue;

    if (!pPropBag)
        return E_INVALIDARG;
    if (SUCCEEDED(SHPropertyBag_ReadBOOL(pPropBag, L"HideNonRemovableDrives", &bValue)))
        m_bHideNonRemovableDrives = bValue;
    if (SUCCEEDED(SHPropertyBag_ReadBOOL(pPropBag, L"ForceAllDrivesRemovable", &bValue)))
        m_bForceAllDrivesRemovable = bValue;
    return S_OK;
}

HRESULT WINAPI CDrivesFolder::Save(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CDrivesFolder::GetValidCharacters(LPWSTR *ppwszValidChars, LPWSTR *ppwszInvalidChars)
{
    if (ppwszValidChars)
        *ppwszValidChars = NULL;
    if (ppwszInvalidChars)
        *ppwszInvalidChars = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI CDrivesFolder::GetMaxLength(LPCWSTR pszName, int *piMaxNameLen)
{
    WCHAR szRoot[4], szFileSystem[MAX_PATH];

    if (!pszName || !piMaxNameLen)
        return E_INVALIDARG;
    if (!(PathGetDriveNumberW(pszName) >= 0 && (!pszName[2] || (pszName[2] == L'\\' && !pszName[3]))))
        return E_INVALIDARG;

    StringCchPrintfW(szRoot, _countof(szRoot), L"%c:\\", pszName[0]);
    if (!GetVolumeInformationW(szRoot, NULL, 0, NULL, NULL, NULL, szFileSystem, _countof(szFileSystem)))
        return HResultFromWin32(GetLastError());

    if (!_wcsnicmp(szFileSystem, L"FAT", 3) || !_wcsicmp(szFileSystem, L"exFAT"))
        *piMaxNameLen = 11;
    else
        *piMaxNameLen = 32;
    return S_OK;
}

HRESULT WINAPI CDrivesFolder::IsTypeSupported(LPCWSTR pszType)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::GetPropertiesToApply(IPropertyStore *pps, REFIID riid, void **ppv)
{
    if (ppv)
        *ppv = NULL;
    return S_FALSE;
}

HRESULT WINAPI CDrivesFolder::CreateNewIDList(LPCWSTR pszType, PCIDLIST_ABSOLUTE pidlFolder, PITEMID_CHILD *ppidl)
{
    if (ppidl)
        *ppidl = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI CDrivesFolder::GetIconOf(PCUITEMID_CHILD pidl, UINT flags, int *pIconIndex)
{
    CComPtr<IExtractIconW> pei;
    WCHAR szIconFile[MAX_PATH];
    INT iIcon = 0;
    UINT uFlags = 0;
    HRESULT hr;

    if (!pidl || !pIconIndex)
        return E_INVALIDARG;

    hr = GetUIObjectOf(NULL, 1, &pidl, IID_NULL_PPV_ARG(IExtractIconW, &pei));
    if (FAILED(hr))
        return S_FALSE;
    hr = pei->GetIconLocation(flags, szIconFile, _countof(szIconFile), &iIcon, &uFlags);
    if (hr != S_OK || (uFlags & (GIL_NOTFILENAME | GIL_DONTCACHE | GIL_PERINSTANCE)))
        return S_FALSE;

    *pIconIndex = SIC_GetIconIndex(szIconFile, iIcon, uFlags);
    return (*pIconIndex >= 0) ? S_OK : S_FALSE;
}

HRESULT WINAPI CDrivesFolder::GetOverlayIndex(PCUITEMID_CHILD pidl, int *pIndex)
{
    return SHELL32_GetItemOverlay(static_cast<IShellFolder2 *>(this), pidlRoot, pidl, pIndex, FALSE);
}

HRESULT WINAPI CDrivesFolder::GetOverlayIconIndex(PCUITEMID_CHILD pidl, int *pIconIndex)
{
    return SHELL32_GetItemOverlay(static_cast<IShellFolder2 *>(this), pidlRoot, pidl, pIconIndex, TRUE);
}

HRESULT WINAPI CDrivesFolder::CreateStream(LPCOLESTR pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2, IStream **ppstm)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::OpenStream(LPCOLESTR pwcsName, void *reserved1, DWORD grfMode, DWORD reserved2, IStream **ppstm)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::CreateStorage(LPCOLESTR pwcsName, DWORD grfMode, DWORD dwStgFmt, DWORD reserved2, IStorage **ppstg)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::OpenStorage(LPCOLESTR pwcsName, IStorage *pstgPriority, DWORD grfMode, SNB snbExclude, DWORD reserved, IStorage **ppstg)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::CopyTo(DWORD ciidExclude, const IID *rgiidExclude, SNB snbExclude, IStorage *pstgDest)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::MoveElementTo(LPCOLESTR pwcsName, IStorage *pstgDest, LPCOLESTR pwcsNewName, DWORD grfFlags)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::Commit(DWORD grfCommitFlags)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::Revert()
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::EnumElements(DWORD reserved1, void *reserved2, DWORD reserved3, IEnumSTATSTG **ppenum)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::DestroyElement(LPCOLESTR pwcsName)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::RenameElement(LPCOLESTR pwcsOldName, LPCOLESTR pwcsNewName)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::SetElementTimes(LPCOLESTR pwcsName, const FILETIME *pctime, const FILETIME *patime, const FILETIME *pmtime)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::SetClass(REFCLSID clsid)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::SetStateBits(DWORD grfStateBits, DWORD grfMask)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::Stat(STATSTG *pstatstg, DWORD grfStatFlag)
{
    return E_FAIL;
}

HRESULT WINAPI CDrivesFolder::SetSite(IUnknown *pUnkSite)
{
    m_site = pUnkSite;
    return S_OK;
}

HRESULT WINAPI CDrivesFolder::GetSite(REFIID riid, void **ppvSite)
{
    if (!ppvSite)
        return E_POINTER;
    *ppvSite = NULL;
    if (!m_site)
        return E_FAIL;
    return m_site->QueryInterface(riid, ppvSite);
}

HRESULT WINAPI CDrivesFolder::RemoveBackReferences()
{
    SetSite(NULL);
    return S_OK;
}

HRESULT WINAPI CDrivesFolder::AddRefBackReference()
{
    InterlockedIncrement(&m_cBackReferences);
    return S_OK;
}

HRESULT WINAPI CDrivesFolder::ReleaseBackReference()
{
    LONG cRefs = m_cBackReferences;
    while (cRefs > 0)
    {
        LONG cPrev = InterlockedCompareExchange(&m_cBackReferences, cRefs - 1, cRefs);
        if (cPrev == cRefs)
            break;
        cRefs = cPrev;
    }
    return S_OK;
}

HRESULT WINAPI CDrivesFolder::SetRemoteComputer(LPCWSTR pszComputer)
{
    m_pszRemoteComputer.Free();
    if (pszComputer)
        return SHStrDupW(pszComputer, &m_pszRemoteComputer);
    return S_OK;
}

/**************************************************************************
 *    CDrivesFolder::ShouldShow
 */
HRESULT WINAPI CDrivesFolder::ShouldShow(IShellFolder *psf, PCIDLIST_ABSOLUTE pidlFolder, PCUITEMID_CHILD pidlItem)
{
    if (const CLSID* pClsid = IsRegItem(pidlItem))
        return SHELL32_IsShellFolderNamespaceItemHidden(L"HideMyComputerIcons", *pClsid) ? S_FALSE : S_OK;
    return S_OK;
}

/**************************************************************************
 *    CDrivesFolder::MessageSFVCB (IShellFolderViewCB)
 */
HRESULT WINAPI CDrivesFolder::MessageSFVCB(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case SFVM_FSNOTIFY:
            if (lParam == SHCNE_DRIVEADD && wParam)
            {
                g_IsFloppyCache = 0;
                INT8 drive = GetDriveNumber(((PIDLIST_ABSOLUTE*)wParam)[0]);
                if (drive >= 0 && ((1UL << drive) & SHRestricted(REST_NODRIVES)))
                    return S_FALSE;
            }
            else if (lParam == SHCNE_DRIVEREMOVED)
            {
                g_IsFloppyCache = 0;
            }
            break;
        #if ROSPOLICY_DRIVESFOLDER_DEFLARGEICONS
        case SFVM_DEFVIEWMODE:
            *((FOLDERVIEWMODE*)lParam) = FVM_ICON;
            return S_OK;
        #endif
    }
    return E_NOTIMPL;
}

/************************************************************************/
/* IContextMenuCB interface */

HRESULT WINAPI CDrivesFolder::CallBack(IShellFolder *psf, HWND hwndOwner, IDataObject *pdtobj, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    enum { IDC_PROPERTIES };
    /* no data object means no selection */
    if (!pdtobj)
    {
        if (uMsg == DFM_INVOKECOMMAND && wParam == IDC_PROPERTIES)
        {
            // "System" properties
            return SHELL_ExecuteControlPanelCPL(hwndOwner, L"sysdm.cpl") ? S_OK : E_FAIL;
        }
        else if (uMsg == DFM_MERGECONTEXTMENU) // TODO: DFM_MERGECONTEXTMENU_BOTTOM
        {
            QCMINFO *pqcminfo = (QCMINFO *)lParam;
            HMENU hpopup = CreatePopupMenu();
            _InsertMenuItemW(hpopup, 0, TRUE, IDC_PROPERTIES, MFT_STRING, MAKEINTRESOURCEW(IDS_PROPERTIES), MFS_ENABLED);
            pqcminfo->idCmdFirst = Shell_MergeMenus(pqcminfo->hmenu, hpopup, pqcminfo->indexMenu, pqcminfo->idCmdFirst, pqcminfo->idCmdLast, MM_ADDSEPARATOR);
            DestroyMenu(hpopup);
            return S_OK;
        }
    }
    return SHELL32_DefaultContextMenuCallBack(psf, pdtobj, uMsg);
}
