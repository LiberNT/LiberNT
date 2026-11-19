/*
 * PROJECT:     LiberNT Shell
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Per-volume $Recycle.Bin with $I/$R item pairs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "recyclebin_private.h"
#include <atlstr.h>
#include <shlwapi.h>
#include <sddl.h>

EXTERN_C HRESULT WINAPI SHUpdateRecycleBinIcon(void);

#define RECYCLE_BIN_DIRECTORY L"$Recycle.Bin"
#define RECYCLE_BIN_CLSID L"{645FF040-5081-101B-9F08-00AA002F954E}"
#define RECYCLE_BIN_NAME_LENGTH 6
#define RECYCLE_BIN_INFO_V1 1
#define RECYCLE_BIN_INFO_V2 2
#define RECYCLE_BIN_INFO_V1_PATH 260
#define RECYCLE_BIN_INFO_MAX_PATH 32768

#pragma pack(push, 1)

typedef struct _RECYCLE_BIN_INFO_HEADER
{
    ULONGLONG Version;
    ULONGLONG FileSize;
    FILETIME DeletionTime;
} RECYCLE_BIN_INFO_HEADER, *PRECYCLE_BIN_INFO_HEADER;

typedef struct _RECYCLE_BIN_INFO_V2_HEADER
{
    RECYCLE_BIN_INFO_HEADER Header;
    DWORD PathLength;
} RECYCLE_BIN_INFO_V2_HEADER, *PRECYCLE_BIN_INFO_V2_HEADER;

#pragma pack(pop)

C_ASSERT(sizeof(RECYCLE_BIN_INFO_HEADER) == 24);
C_ASSERT(sizeof(RECYCLE_BIN_INFO_V2_HEADER) == 28);

static BOOL
RecycleBinIsDots(_In_ LPCWSTR Name)
{
    return Name[0] == L'.' && (Name[1] == UNICODE_NULL || (Name[1] == L'.' && Name[2] == UNICODE_NULL));
}

static ULONGLONG
RecycleBinTreeSize(_In_ const CStringW &Path)
{
    WIN32_FIND_DATAW FindData;
    ULONGLONG Size = 0;
    HANDLE hFind = FindFirstFileW(Path + L"\\*", &FindData);

    if (hFind == INVALID_HANDLE_VALUE)
        return 0;

    do
    {
        if (RecycleBinIsDots(FindData.cFileName))
            continue;

        if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(FindData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            Size += RecycleBinTreeSize(Path + L"\\" + FindData.cFileName);
        }
        else if (!(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            Size += ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        }
    } while (FindNextFileW(hFind, &FindData));

    FindClose(hFind);
    return Size;
}

static DWORD
RecycleBinRemoveTree(_In_ const CStringW &Path)
{
    DWORD Attributes = GetFileAttributesW(Path);
    if (Attributes == INVALID_FILE_ATTRIBUTES)
    {
        DWORD Error = GetLastError();
        if (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND)
            return ERROR_SUCCESS;
        return Error;
    }

    if (Attributes & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesW(Path, Attributes & ~FILE_ATTRIBUTE_READONLY);

    if (!(Attributes & FILE_ATTRIBUTE_DIRECTORY))
        return DeleteFileW(Path) ? ERROR_SUCCESS : GetLastError();

    if (!(Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        WIN32_FIND_DATAW FindData;
        HANDLE hFind = FindFirstFileW(Path + L"\\*", &FindData);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (RecycleBinIsDots(FindData.cFileName))
                    continue;
                DWORD Error = RecycleBinRemoveTree(Path + L"\\" + FindData.cFileName);
                if (Error != ERROR_SUCCESS)
                {
                    FindClose(hFind);
                    return Error;
                }
            } while (FindNextFileW(hFind, &FindData));
            FindClose(hFind);
        }
    }

    return RemoveDirectoryW(Path) ? ERROR_SUCCESS : GetLastError();
}

static void
RecycleBinRandomName(_Out_writes_(RECYCLE_BIN_NAME_LENGTH + 1) LPWSTR Name)
{
    static const WCHAR Alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static LONG Sequence;
    LARGE_INTEGER Counter;

    QueryPerformanceCounter(&Counter);
    ULONG Seed = Counter.LowPart ^ Counter.HighPart ^ GetTickCount() ^
                 (GetCurrentProcessId() << 8) ^ (GetCurrentThreadId() << 20) ^
                 ((ULONG)InterlockedIncrement(&Sequence) * 0x9E3779B9u);

    for (UINT i = 0; i < RECYCLE_BIN_NAME_LENGTH; i++)
    {
        Seed = Seed * 1664525u + 1013904223u;
        Name[i] = Alphabet[(Seed >> 16) % (_countof(Alphabet) - 1)];
    }
    Name[RECYCLE_BIN_NAME_LENGTH] = UNICODE_NULL;
}

static HRESULT
RecycleBinGetUserSid(_Out_ CStringW &SidString)
{
    HANDLE hToken;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hToken))
    {
        if (GetLastError() != ERROR_NO_TOKEN)
            return HRESULT_FROM_WIN32(GetLastError());
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
            return HRESULT_FROM_WIN32(GetLastError());
    }

    DWORD Length = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &Length);
    PTOKEN_USER User = (PTOKEN_USER)HeapAlloc(GetProcessHeap(), 0, Length);
    if (!User)
    {
        CloseHandle(hToken);
        return E_OUTOFMEMORY;
    }

    HRESULT hr = S_OK;
    LPWSTR String = NULL;
    if (!GetTokenInformation(hToken, TokenUser, User, Length, &Length) ||
        !ConvertSidToStringSidW(User->User.Sid, &String))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
    }
    else
    {
        SidString = String;
        LocalFree(String);
    }

    HeapFree(GetProcessHeap(), 0, User);
    CloseHandle(hToken);
    return hr;
}

class RecycleBinVolumeFile : public IRecycleBinFile
{
public:
    RecycleBinVolumeFile() : m_ref(1), m_FileSize(0)
    {
        m_DeletionTime.dwLowDateTime = m_DeletionTime.dwHighDateTime = 0;
    }
    virtual ~RecycleBinVolumeFile() {}

    HRESULT Init(_In_ const CStringW &Volume, _In_ const CStringW &Folder, _In_ LPCWSTR InfoName);

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP IsEqualIdentity(const RECYCLEBINFILEIDENTITY *pFI) override;
    STDMETHODIMP GetInfo(PDELETED_FILE_INFO pInfo) override;
    STDMETHODIMP GetLastModificationTime(FILETIME *pLastModificationTime) override;
    STDMETHODIMP GetDeletionTime(FILETIME *pDeletionTime) override;
    STDMETHODIMP GetFileSize(ULARGE_INTEGER *pFileSize) override;
    STDMETHODIMP GetPhysicalFileSize(ULARGE_INTEGER *pPhysicalFileSize) override;
    STDMETHODIMP GetAttributes(DWORD *pAttributes) override;
    STDMETHODIMP GetFileName(SIZE_T BufferSize, LPWSTR Buffer, SIZE_T *RequiredSize) override;
    STDMETHODIMP Delete() override;
    STDMETHODIMP Restore() override;
    STDMETHODIMP RemoveFromDatabase() override;

private:
    LONG m_ref;
    CStringW m_Volume;
    CStringW m_InfoPath;
    CStringW m_DataPath;
    CStringW m_OriginalPath;
    FILETIME m_DeletionTime;
    ULONGLONG m_FileSize;

    HRESULT QueryData(_Out_ WIN32_FILE_ATTRIBUTE_DATA *pData);
};

HRESULT
RecycleBinVolumeFile::Init(_In_ const CStringW &Volume, _In_ const CStringW &Folder, _In_ LPCWSTR InfoName)
{
    if (InfoName[0] != L'$' || (InfoName[1] != L'I' && InfoName[1] != L'i'))
        return E_INVALIDARG;

    m_Volume = Volume;
    m_InfoPath = Folder + L"\\" + InfoName;
    m_DataPath = Folder + L"\\$R" + (InfoName + 2);

    HANDLE hInfo = CreateFileW(m_InfoPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hInfo == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());

    LARGE_INTEGER Size;
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (GetFileSizeEx(hInfo, &Size) &&
        Size.QuadPart >= (LONGLONG)sizeof(RECYCLE_BIN_INFO_HEADER) &&
        Size.QuadPart <= (LONGLONG)(sizeof(RECYCLE_BIN_INFO_V2_HEADER) + RECYCLE_BIN_INFO_MAX_PATH * sizeof(WCHAR)))
    {
        DWORD Length = (DWORD)Size.QuadPart;
        PBYTE Buffer = (PBYTE)HeapAlloc(GetProcessHeap(), 0, Length);
        DWORD Read;
        if (!Buffer)
        {
            hr = E_OUTOFMEMORY;
        }
        else if (ReadFile(hInfo, Buffer, Length, &Read, NULL) && Read == Length)
        {
            PRECYCLE_BIN_INFO_HEADER Header = (PRECYCLE_BIN_INFO_HEADER)Buffer;
            LPCWSTR Path = NULL;
            DWORD PathLength = 0;

            if (Header->Version == RECYCLE_BIN_INFO_V1 &&
                Length == sizeof(*Header) + RECYCLE_BIN_INFO_V1_PATH * sizeof(WCHAR))
            {
                Path = (LPCWSTR)(Header + 1);
                PathLength = RECYCLE_BIN_INFO_V1_PATH;
            }
            else if (Header->Version == RECYCLE_BIN_INFO_V2 && Length >= sizeof(RECYCLE_BIN_INFO_V2_HEADER))
            {
                PRECYCLE_BIN_INFO_V2_HEADER Header2 = (PRECYCLE_BIN_INFO_V2_HEADER)Buffer;
                if (Header2->PathLength != 0 &&
                    Header2->PathLength <= RECYCLE_BIN_INFO_MAX_PATH &&
                    Length == sizeof(*Header2) + Header2->PathLength * sizeof(WCHAR))
                {
                    Path = (LPCWSTR)(Header2 + 1);
                    PathLength = Header2->PathLength;
                }
            }

            if (Path && wmemchr(Path, UNICODE_NULL, PathLength) && Path[0] != UNICODE_NULL)
            {
                m_OriginalPath = Path;
                m_FileSize = Header->FileSize;
                m_DeletionTime = Header->DeletionTime;
                hr = S_OK;
            }
        }
        if (Buffer)
            HeapFree(GetProcessHeap(), 0, Buffer);
    }

    CloseHandle(hInfo);
    return hr;
}

STDMETHODIMP RecycleBinVolumeFile::QueryInterface(REFIID riid, void **ppvObject)
{
    if (!ppvObject)
        return E_POINTER;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IRecycleBinFile))
    {
        *ppvObject = static_cast<IRecycleBinFile *>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) RecycleBinVolumeFile::AddRef()
{
    return InterlockedIncrement(&m_ref);
}

STDMETHODIMP_(ULONG) RecycleBinVolumeFile::Release()
{
    ULONG refCount = InterlockedDecrement(&m_ref);
    if (refCount == 0)
        delete this;
    return refCount;
}

HRESULT RecycleBinVolumeFile::QueryData(_Out_ WIN32_FILE_ATTRIBUTE_DATA *pData)
{
    if (!GetFileAttributesExW(m_DataPath, GetFileExInfoStandard, pData))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

STDMETHODIMP RecycleBinVolumeFile::IsEqualIdentity(const RECYCLEBINFILEIDENTITY *pFI)
{
    if (!pFI || !pFI->RecycledFullPath)
        return E_POINTER;

    if (CompareFileTime(&pFI->DeletionTime, &m_DeletionTime) == 0 &&
        _wcsicmp(pFI->RecycledFullPath, m_DataPath) == 0)
    {
        return S_OK;
    }
    return S_FALSE;
}

STDMETHODIMP RecycleBinVolumeFile::GetInfo(PDELETED_FILE_INFO pInfo)
{
    if (!pInfo)
        return E_POINTER;

    WIN32_FILE_ATTRIBUTE_DATA Data;
    HRESULT hr = QueryData(&Data);
    if (FAILED(hr))
        return hr;

    pInfo->LastModification = Data.ftLastWriteTime;
    pInfo->DeletionTime = m_DeletionTime;
    pInfo->FileSize = (RECYCLEBINFILESIZETYPE)min(m_FileSize, (ULONGLONG)MAXUINT);
    pInfo->Attributes = Data.dwFileAttributes;
    InitializeRecycleBinStringRef(&pInfo->OriginalFullPath, m_OriginalPath);
    InitializeRecycleBinStringRef(&pInfo->RecycledFullPath, m_DataPath);
    return S_OK;
}

STDMETHODIMP RecycleBinVolumeFile::GetLastModificationTime(FILETIME *pLastModificationTime)
{
    if (!pLastModificationTime)
        return E_POINTER;

    WIN32_FILE_ATTRIBUTE_DATA Data;
    HRESULT hr = QueryData(&Data);
    if (SUCCEEDED(hr))
        *pLastModificationTime = Data.ftLastWriteTime;
    return hr;
}

STDMETHODIMP RecycleBinVolumeFile::GetDeletionTime(FILETIME *pDeletionTime)
{
    if (!pDeletionTime)
        return E_POINTER;
    *pDeletionTime = m_DeletionTime;
    return S_OK;
}

STDMETHODIMP RecycleBinVolumeFile::GetFileSize(ULARGE_INTEGER *pFileSize)
{
    if (!pFileSize)
        return E_POINTER;
    pFileSize->QuadPart = m_FileSize;
    return S_OK;
}

STDMETHODIMP RecycleBinVolumeFile::GetPhysicalFileSize(ULARGE_INTEGER *pPhysicalFileSize)
{
    if (!pPhysicalFileSize)
        return E_POINTER;

    DWORD SectorsPerCluster, BytesPerSector, FreeClusters, TotalClusters;
    if (!GetDiskFreeSpaceW(m_Volume, &SectorsPerCluster, &BytesPerSector, &FreeClusters, &TotalClusters))
        return HRESULT_FROM_WIN32(GetLastError());

    ULONGLONG ClusterSize = (ULONGLONG)SectorsPerCluster * BytesPerSector;
    pPhysicalFileSize->QuadPart = ClusterSize ? ROUND_UP(m_FileSize, ClusterSize) : m_FileSize;
    return S_OK;
}

STDMETHODIMP RecycleBinVolumeFile::GetAttributes(DWORD *pAttributes)
{
    if (!pAttributes)
        return E_POINTER;

    WIN32_FILE_ATTRIBUTE_DATA Data;
    HRESULT hr = QueryData(&Data);
    if (SUCCEEDED(hr))
        *pAttributes = Data.dwFileAttributes;
    return hr;
}

STDMETHODIMP RecycleBinVolumeFile::GetFileName(SIZE_T BufferSize, LPWSTR Buffer, SIZE_T *RequiredSize)
{
    SIZE_T Required = ((SIZE_T)m_OriginalPath.GetLength() + 1) * sizeof(WCHAR);

    if (RequiredSize)
        *RequiredSize = Required;
    if (BufferSize == 0 && !Buffer)
        return S_OK;
    if (!Buffer)
        return E_POINTER;
    if (BufferSize < Required)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    CopyMemory(Buffer, (LPCWSTR)m_OriginalPath, Required);
    return S_OK;
}

STDMETHODIMP RecycleBinVolumeFile::Delete()
{
    DWORD Error = RecycleBinRemoveTree(m_DataPath);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);

    HRESULT hr = RemoveFromDatabase();
    SHUpdateRecycleBinIcon();
    return hr;
}

STDMETHODIMP RecycleBinVolumeFile::Restore()
{
    WCHAR From[MAX_PATH + 1], To[MAX_PATH + 1];
    if (m_DataPath.GetLength() >= MAX_PATH || m_OriginalPath.GetLength() >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);

    ZeroMemory(From, sizeof(From));
    ZeroMemory(To, sizeof(To));
    CopyMemory(From, (LPCWSTR)m_DataPath, m_DataPath.GetLength() * sizeof(WCHAR));
    CopyMemory(To, (LPCWSTR)m_OriginalPath, m_OriginalPath.GetLength() * sizeof(WCHAR));

    SHFILEOPSTRUCTW Operation = { NULL, FO_MOVE, From, To, FOF_NOCONFIRMMKDIR };
    int Result = SHFileOperationW(&Operation);
    if (Result != 0)
        return HRESULT_FROM_WIN32(Result);
    if (Operation.fAnyOperationsAborted)
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);

    HRESULT hr = RemoveFromDatabase();
    SHUpdateRecycleBinIcon();
    return hr;
}

STDMETHODIMP RecycleBinVolumeFile::RemoveFromDatabase()
{
    if (DeleteFileW(m_InfoPath))
        return S_OK;

    DWORD Error = GetLastError();
    if (Error == ERROR_FILE_NOT_FOUND)
        return S_OK;
    return HRESULT_FROM_WIN32(Error);
}

class RecycleBinVolumeEnum : public IRecycleBinEnumList
{
public:
    RecycleBinVolumeEnum(_In_ const CStringW &Volume, _In_ const CStringW &Folder)
        : m_ref(1), m_Volume(Volume), m_Folder(Folder), m_hFind(INVALID_HANDLE_VALUE), m_Done(FALSE)
    {
    }
    virtual ~RecycleBinVolumeEnum()
    {
        if (m_hFind != INVALID_HANDLE_VALUE)
            FindClose(m_hFind);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Next(DWORD celt, IRecycleBinFile **rgelt, DWORD *pceltFetched) override;
    STDMETHODIMP Skip(DWORD celt) override;
    STDMETHODIMP Reset() override;

private:
    LONG m_ref;
    CStringW m_Volume;
    CStringW m_Folder;
    HANDLE m_hFind;
    BOOL m_Done;

    HRESULT NextItem(_Out_ RecycleBinVolumeFile **ppFile);
};

STDMETHODIMP RecycleBinVolumeEnum::QueryInterface(REFIID riid, void **ppvObject)
{
    if (!ppvObject)
        return E_POINTER;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IRecycleBinEnumList))
    {
        *ppvObject = static_cast<IRecycleBinEnumList *>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) RecycleBinVolumeEnum::AddRef()
{
    return InterlockedIncrement(&m_ref);
}

STDMETHODIMP_(ULONG) RecycleBinVolumeEnum::Release()
{
    ULONG refCount = InterlockedDecrement(&m_ref);
    if (refCount == 0)
        delete this;
    return refCount;
}

HRESULT RecycleBinVolumeEnum::NextItem(_Out_ RecycleBinVolumeFile **ppFile)
{
    *ppFile = NULL;

    while (!m_Done)
    {
        WIN32_FIND_DATAW FindData;
        if (m_hFind == INVALID_HANDLE_VALUE)
        {
            m_hFind = FindFirstFileW(m_Folder + L"\\$I*", &FindData);
            if (m_hFind == INVALID_HANDLE_VALUE)
            {
                m_Done = TRUE;
                break;
            }
        }
        else if (!FindNextFileW(m_hFind, &FindData))
        {
            m_Done = TRUE;
            break;
        }

        if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        RecycleBinVolumeFile *pFile = new RecycleBinVolumeFile();
        if (!pFile)
            return E_OUTOFMEMORY;

        DWORD Attributes;
        if (SUCCEEDED(pFile->Init(m_Volume, m_Folder, FindData.cFileName)) &&
            SUCCEEDED(pFile->GetAttributes(&Attributes)))
        {
            *ppFile = pFile;
            return S_OK;
        }
        pFile->Release();
    }

    return S_FALSE;
}

STDMETHODIMP RecycleBinVolumeEnum::Next(DWORD celt, IRecycleBinFile **rgelt, DWORD *pceltFetched)
{
    if (!rgelt)
        return E_POINTER;
    if (!pceltFetched && celt > 1)
        return E_INVALIDARG;

    DWORD Fetched = 0;
    while (Fetched < celt)
    {
        RecycleBinVolumeFile *pFile;
        HRESULT hr = NextItem(&pFile);
        if (FAILED(hr))
        {
            while (Fetched > 0)
                rgelt[--Fetched]->Release();
            return hr;
        }
        if (hr != S_OK)
            break;
        rgelt[Fetched++] = pFile;
    }

    if (pceltFetched)
        *pceltFetched = Fetched;
    return Fetched == celt ? S_OK : S_FALSE;
}

STDMETHODIMP RecycleBinVolumeEnum::Skip(DWORD celt)
{
    while (celt-- > 0)
    {
        RecycleBinVolumeFile *pFile;
        HRESULT hr = NextItem(&pFile);
        if (hr != S_OK)
            return FAILED(hr) ? hr : S_FALSE;
        pFile->Release();
    }
    return S_OK;
}

STDMETHODIMP RecycleBinVolumeEnum::Reset()
{
    if (m_hFind != INVALID_HANDLE_VALUE)
        FindClose(m_hFind);
    m_hFind = INVALID_HANDLE_VALUE;
    m_Done = FALSE;
    return S_OK;
}

class RecycleBinVolume : public IRecycleBin
{
public:
    RecycleBinVolume() : m_ref(1) {}
    virtual ~RecycleBinVolume() {}

    HRESULT Init(_In_ LPCWSTR VolumePath);

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP DeleteFile(LPCWSTR szFileName) override;
    STDMETHODIMP EmptyRecycleBin() override;
    STDMETHODIMP EnumObjects(IRecycleBinEnumList **ppEnumList) override;
    STDMETHODIMP GetDirectory(LPWSTR szPath) override;

private:
    LONG m_ref;
    CStringW m_Volume;
    CStringW m_Root;
    CStringW m_Folder;
    CStringW m_Sid;

    HRESULT EnsureFolder();
};

HRESULT RecycleBinVolume::Init(_In_ LPCWSTR VolumePath)
{
    HRESULT hr = RecycleBinGetUserSid(m_Sid);
    if (FAILED(hr))
        return hr;

    m_Volume = VolumePath;
    if (m_Volume.IsEmpty())
        return E_INVALIDARG;
    if (m_Volume[m_Volume.GetLength() - 1] != L'\\')
        m_Volume += L'\\';

    m_Root = m_Volume + RECYCLE_BIN_DIRECTORY;
    m_Folder = m_Root + L"\\" + m_Sid;
    return S_OK;
}

HRESULT RecycleBinVolume::EnsureFolder()
{
    if (!CreateDirectoryW(m_Root, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    SetFileAttributesW(m_Root, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

    DWORD Attributes = GetFileAttributesW(m_Folder);
    if (Attributes != INVALID_FILE_ATTRIBUTES)
        return (Attributes & FILE_ATTRIBUTE_DIRECTORY) ? S_OK : HRESULT_FROM_WIN32(ERROR_DIRECTORY);

    CStringW Sddl;
    Sddl.Format(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;%s)", (LPCWSTR)m_Sid);

    SECURITY_ATTRIBUTES Security = { sizeof(Security), NULL, FALSE };
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, SDDL_REVISION_1,
                                                              &Security.lpSecurityDescriptor, NULL))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    BOOL Created = CreateDirectoryW(m_Folder, &Security);
    DWORD Error = Created ? ERROR_SUCCESS : GetLastError();
    LocalFree(Security.lpSecurityDescriptor);
    if (!Created && Error != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(Error);

    CStringW DesktopIni = m_Folder + L"\\desktop.ini";
    if (WritePrivateProfileStringW(L".ShellClassInfo", L"CLSID", RECYCLE_BIN_CLSID, DesktopIni))
        SetFileAttributesW(DesktopIni, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    SetFileAttributesW(m_Folder, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return S_OK;
}

STDMETHODIMP RecycleBinVolume::QueryInterface(REFIID riid, void **ppvObject)
{
    if (!ppvObject)
        return E_POINTER;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IRecycleBin))
    {
        *ppvObject = static_cast<IRecycleBin *>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) RecycleBinVolume::AddRef()
{
    return InterlockedIncrement(&m_ref);
}

STDMETHODIMP_(ULONG) RecycleBinVolume::Release()
{
    ULONG refCount = InterlockedDecrement(&m_ref);
    if (refCount == 0)
        delete this;
    return refCount;
}

STDMETHODIMP RecycleBinVolume::DeleteFile(LPCWSTR szFileName)
{
    TRACE("(%p, %s)\n", this, debugstr_w(szFileName));

    if (!szFileName)
        return E_POINTER;

    CStringW FullName = szFileName;
    if (FullName.GetLength() <= m_Volume.GetLength() ||
        _wcsnicmp(FullName, m_Volume, m_Volume.GetLength()) != 0)
    {
        return E_INVALIDARG;
    }
    FullName.TrimRight(L'\\');

    CStringW RootPrefix = m_Root + L"\\";
    if (FullName.CompareNoCase(m_Root) == 0 ||
        _wcsnicmp(FullName, RootPrefix, RootPrefix.GetLength()) == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    }

    WIN32_FIND_DATAW FindData;
    HANDLE hFind = FindFirstFileW(FullName, &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    FindClose(hFind);

    ULONGLONG FileSize;
    if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(FindData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        FileSize = RecycleBinTreeSize(FullName);
    }
    else
    {
        FileSize = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
    }

    HRESULT hr = EnsureFolder();
    if (FAILED(hr))
        return hr;

    /* TODO: honour the BitBucket MaxCapacity and NukeOnDelete settings of the volume */

    DWORD PathLength = FullName.GetLength() + 1;
    DWORD InfoLength = sizeof(RECYCLE_BIN_INFO_V2_HEADER) + PathLength * sizeof(WCHAR);
    PRECYCLE_BIN_INFO_V2_HEADER Info = (PRECYCLE_BIN_INFO_V2_HEADER)HeapAlloc(GetProcessHeap(), 0, InfoLength);
    if (!Info)
        return E_OUTOFMEMORY;

    Info->Header.Version = RECYCLE_BIN_INFO_V2;
    Info->Header.FileSize = FileSize;
    GetSystemTimeAsFileTime(&Info->Header.DeletionTime);
    Info->PathLength = PathLength;
    CopyMemory(Info + 1, (LPCWSTR)FullName, PathLength * sizeof(WCHAR));

    LPCWSTR Extension = PathFindExtensionW(PathFindFileNameW(FullName));
    CStringW InfoPath, DataPath;
    HANDLE hInfo = INVALID_HANDLE_VALUE;
    DWORD Error = ERROR_FILE_EXISTS;

    for (UINT Attempt = 0; Attempt < 64 && hInfo == INVALID_HANDLE_VALUE; Attempt++)
    {
        WCHAR Name[RECYCLE_BIN_NAME_LENGTH + 1];
        RecycleBinRandomName(Name);
        InfoPath = m_Folder + L"\\$I" + Name + Extension;
        DataPath = m_Folder + L"\\$R" + Name + Extension;

        if (GetFileAttributesW(DataPath) != INVALID_FILE_ATTRIBUTES)
            continue;

        hInfo = CreateFileW(InfoPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_ARCHIVE, NULL);
        if (hInfo == INVALID_HANDLE_VALUE)
        {
            Error = GetLastError();
            if (Error != ERROR_FILE_EXISTS)
                break;
        }
    }

    if (hInfo == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, Info);
        return HRESULT_FROM_WIN32(Error);
    }

    DWORD Written;
    BOOL Ok = WriteFile(hInfo, Info, InfoLength, &Written, NULL) && Written == InfoLength;
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(hInfo);

    if (Ok && !MoveFileExW(FullName, DataPath, 0))
    {
        Ok = FALSE;
        Error = GetLastError();
    }

    if (!Ok)
    {
        DeleteFileW(InfoPath);
        HeapFree(GetProcessHeap(), 0, Info);
        return HRESULT_FROM_WIN32(Error);
    }

    RECYCLEBINFILEIDENTITY Identity = { Info->Header.DeletionTime, DataPath };
    CRecycleBin_NotifyRecycled(FullName, &FindData, &Identity);
    HeapFree(GetProcessHeap(), 0, Info);
    SHUpdateRecycleBinIcon();
    return S_OK;
}

STDMETHODIMP RecycleBinVolume::EmptyRecycleBin()
{
    TRACE("(%p)\n", this);

    WIN32_FIND_DATAW FindData;
    HANDLE hFind = FindFirstFileW(m_Folder + L"\\$*", &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        DWORD Error = GetLastError();
        if (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND)
            return S_OK;
        return HRESULT_FROM_WIN32(Error);
    }

    DWORD Error = ERROR_SUCCESS;
    do
    {
        WCHAR Kind = FindData.cFileName[1];
        if (Kind != L'R' && Kind != L'r' && Kind != L'I' && Kind != L'i')
            continue;

        DWORD ItemError = RecycleBinRemoveTree(m_Folder + L"\\" + FindData.cFileName);
        if (ItemError != ERROR_SUCCESS && Error == ERROR_SUCCESS)
            Error = ItemError;
    } while (FindNextFileW(hFind, &FindData));
    FindClose(hFind);

    SHUpdateRecycleBinIcon();
    return HRESULT_FROM_WIN32(Error);
}

STDMETHODIMP RecycleBinVolume::EnumObjects(IRecycleBinEnumList **ppEnumList)
{
    if (!ppEnumList)
        return E_POINTER;

    *ppEnumList = new RecycleBinVolumeEnum(m_Volume, m_Folder);
    return *ppEnumList ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP RecycleBinVolume::GetDirectory(LPWSTR szPath)
{
    if (!szPath)
        return E_POINTER;

    HRESULT hr = EnsureFolder();
    if (FAILED(hr))
        return hr;

    lstrcpynW(szPath, m_Folder, MAX_PATH);
    return S_OK;
}

EXTERN_C
HRESULT RecycleBinVolume_Constructor(_In_ LPCWSTR VolumePath, _Out_ IUnknown **ppUnknown)
{
    if (!VolumePath || !ppUnknown)
        return E_POINTER;
    *ppUnknown = NULL;

    RecycleBinVolume *pThis = new RecycleBinVolume();
    if (!pThis)
        return E_OUTOFMEMORY;

    HRESULT hr = pThis->Init(VolumePath);
    if (FAILED(hr))
    {
        pThis->Release();
        return hr;
    }

    *ppUnknown = static_cast<IRecycleBin *>(pThis);
    return S_OK;
}
