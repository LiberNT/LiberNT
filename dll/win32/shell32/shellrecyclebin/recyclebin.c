/*
 * PROJECT:     LiberNT Shell
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Flat recycle bin API over the recycle bin objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "recyclebin_private.h"

static
BOOL
RecycleBinSucceeded(
    _In_ HRESULT hr)
{
    if (SUCCEEDED(hr))
        return TRUE;

    if (HRESULT_FACILITY(hr) == FACILITY_WIN32)
        SetLastError(HRESULT_CODE(hr));
    else if (hr == E_OUTOFMEMORY)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    else
        SetLastError(ERROR_GEN_FAILURE);
    return FALSE;
}

static
LPWSTR
RecycleBinAnsiToWide(
    _In_opt_ LPCSTR String,
    _Out_ BOOL *Failed)
{
    LPWSTR Wide;
    INT Length;

    *Failed = FALSE;
    if (String == NULL)
        return NULL;

    Length = MultiByteToWideChar(CP_ACP, 0, String, -1, NULL, 0);
    if (Length == 0)
    {
        *Failed = TRUE;
        return NULL;
    }

    Wide = HeapAlloc(GetProcessHeap(), 0, Length * sizeof(WCHAR));
    if (Wide == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        *Failed = TRUE;
        return NULL;
    }

    if (MultiByteToWideChar(CP_ACP, 0, String, -1, Wide, Length) == 0)
    {
        HeapFree(GetProcessHeap(), 0, Wide);
        *Failed = TRUE;
        return NULL;
    }

    return Wide;
}

static
BOOL
RecycleBinVolumeRoot(
    _In_ LPCWSTR Path,
    _Out_writes_(MAX_PATH) LPWSTR Root)
{
    if (Path[0] == L'\\' && Path[1] == L'\\' && (Path[2] == L'?' || Path[2] == L'.') && Path[3] == L'\\')
        Path += 4;

    if (((Path[0] >= L'A' && Path[0] <= L'Z') || (Path[0] >= L'a' && Path[0] <= L'z')) && Path[1] == L':')
    {
        Root[0] = Path[0];
        Root[1] = L':';
        Root[2] = L'\\';
        Root[3] = UNICODE_NULL;
        return TRUE;
    }

    return GetVolumePathNameW(Path, Root, MAX_PATH);
}

EXTERN_C
HRESULT
GetDefaultRecycleBin(
    IN LPCWSTR pszVolume OPTIONAL,
    OUT IRecycleBin **pprb)
{
    WCHAR Root[MAX_PATH];
    IUnknown *Unknown;
    HRESULT hr;

    if (pprb == NULL)
        return E_POINTER;
    *pprb = NULL;

    if (pszVolume == NULL)
    {
        hr = RecycleBinGeneric_Constructor(&Unknown);
    }
    else
    {
        if (!RecycleBinVolumeRoot(pszVolume, Root))
            return HRESULT_FROM_WIN32(GetLastError());
        hr = RecycleBin5_Constructor(Root, &Unknown);
    }

    if (FAILED(hr))
        return hr;

    hr = IUnknown_QueryInterface(Unknown, &IID_IRecycleBin, (void **)pprb);
    IUnknown_Release(Unknown);
    return hr;
}

EXTERN_C
HRESULT
GetRecycleBinPathFromDriveNumber(
    UINT Drive,
    LPWSTR Path)
{
    WCHAR Root[4];
    IRecycleBin *RecycleBin;
    HRESULT hr;

    if (Drive >= RECYCLEBINMAXDRIVECOUNT)
        return E_INVALIDARG;

    Root[0] = (WCHAR)(L'A' + Drive);
    Root[1] = L':';
    Root[2] = L'\\';
    Root[3] = UNICODE_NULL;
    if (GetDriveTypeW(Root) != DRIVE_FIXED)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DRIVE);

    hr = GetDefaultRecycleBin(Root, &RecycleBin);
    if (FAILED(hr))
        return hr;

    hr = IRecycleBin_GetDirectory(RecycleBin, Path);
    IRecycleBin_Release(RecycleBin);
    return hr;
}

BOOL
WINAPI
CloseRecycleBinHandle(
    IN HDELFILE hDeletedFile)
{
    IRecycleBinFile_Release(IRecycleBinFileFromHDELFILE(hDeletedFile));
    return TRUE;
}

BOOL
WINAPI
DeleteFileToRecycleBinW(
    IN LPCWSTR FileName)
{
    IRecycleBin *RecycleBin;
    HRESULT hr;

    hr = GetDefaultRecycleBin(NULL, &RecycleBin);
    if (SUCCEEDED(hr))
    {
        hr = IRecycleBin_DeleteFile(RecycleBin, FileName);
        IRecycleBin_Release(RecycleBin);
    }

    return RecycleBinSucceeded(hr);
}

BOOL
WINAPI
DeleteFileToRecycleBinA(
    IN LPCSTR FileName)
{
    LPWSTR Wide;
    BOOL Failed;
    BOOL Result;

    if (FileName == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Wide = RecycleBinAnsiToWide(FileName, &Failed);
    if (Failed)
        return FALSE;

    Result = DeleteFileToRecycleBinW(Wide);
    HeapFree(GetProcessHeap(), 0, Wide);
    return Result;
}

BOOL
WINAPI
DeleteFileInRecycleBin(
    IN HDELFILE hDeletedFile)
{
    return RecycleBinSucceeded(IRecycleBinFile_Delete(IRecycleBinFileFromHDELFILE(hDeletedFile)));
}

BOOL
WINAPI
RestoreFileFromRecycleBin(
    IN HDELFILE hDeletedFile)
{
    return RecycleBinSucceeded(IRecycleBinFile_Restore(IRecycleBinFileFromHDELFILE(hDeletedFile)));
}

BOOL
WINAPI
EmptyRecycleBinW(
    IN LPCWSTR pszRoot OPTIONAL)
{
    IRecycleBin *RecycleBin;
    HRESULT hr;

    hr = GetDefaultRecycleBin(pszRoot, &RecycleBin);
    if (SUCCEEDED(hr))
    {
        hr = IRecycleBin_EmptyRecycleBin(RecycleBin);
        IRecycleBin_Release(RecycleBin);
    }

    return RecycleBinSucceeded(hr);
}

BOOL
WINAPI
EmptyRecycleBinA(
    IN LPCSTR pszRoot OPTIONAL)
{
    LPWSTR Wide;
    BOOL Failed;
    BOOL Result;

    Wide = RecycleBinAnsiToWide(pszRoot, &Failed);
    if (Failed)
        return FALSE;

    Result = EmptyRecycleBinW(Wide);
    HeapFree(GetProcessHeap(), 0, Wide);
    return Result;
}

BOOL
WINAPI
EnumerateRecycleBinW(
    IN LPCWSTR pszRoot OPTIONAL,
    IN PENUMERATE_RECYCLEBIN_CALLBACK pFnCallback,
    IN PVOID Context OPTIONAL)
{
    IRecycleBin *RecycleBin;
    IRecycleBinEnumList *List;
    IRecycleBinFile *File;
    HRESULT hr;

    if (pFnCallback == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    hr = GetDefaultRecycleBin(pszRoot, &RecycleBin);
    if (FAILED(hr))
        return RecycleBinSucceeded(hr);

    hr = IRecycleBin_EnumObjects(RecycleBin, &List);
    IRecycleBin_Release(RecycleBin);
    if (FAILED(hr))
        return RecycleBinSucceeded(hr);

    for (;;)
    {
        File = NULL;
        hr = IRecycleBinEnumList_Next(List, 1, &File, NULL);
        if (hr != S_OK)
            break;

        if (!pFnCallback(Context, (HDELFILE)File))
            break;
    }

    IRecycleBinEnumList_Release(List);
    return RecycleBinSucceeded(hr == S_FALSE ? S_OK : hr);
}

BOOL
WINAPI
EnumerateRecycleBinA(
    IN LPCSTR pszRoot OPTIONAL,
    IN PENUMERATE_RECYCLEBIN_CALLBACK pFnCallback,
    IN PVOID Context OPTIONAL)
{
    LPWSTR Wide;
    BOOL Failed;
    BOOL Result;

    Wide = RecycleBinAnsiToWide(pszRoot, &Failed);
    if (Failed)
        return FALSE;

    Result = EnumerateRecycleBinW(Wide, pFnCallback, Context);
    HeapFree(GetProcessHeap(), 0, Wide);
    return Result;
}

EXTERN_C
HDELFILE
GetRecycleBinFileHandle(
    IN LPCWSTR pszRoot OPTIONAL,
    IN const RECYCLEBINFILEIDENTITY *pFI)
{
    IRecycleBin *RecycleBin;
    IRecycleBinEnumList *List;
    IRecycleBinFile *File;
    HRESULT hr;

    if (FAILED(GetDefaultRecycleBin(pszRoot, &RecycleBin)))
        return NULL;

    hr = IRecycleBin_EnumObjects(RecycleBin, &List);
    IRecycleBin_Release(RecycleBin);
    if (FAILED(hr))
        return NULL;

    for (;;)
    {
        File = NULL;
        if (IRecycleBinEnumList_Next(List, 1, &File, NULL) != S_OK)
        {
            File = NULL;
            break;
        }

        if (IRecycleBinFile_IsEqualIdentity(File, pFI) == S_OK)
            break;

        IRecycleBinFile_Release(File);
    }

    IRecycleBinEnumList_Release(List);
    return (HDELFILE)File;
}

EXTERN_C
BOOL
RemoveFromRecycleBinDatabase(
    IN const RECYCLEBINFILEIDENTITY *pFI)
{
    WCHAR Root[MAX_PATH];
    HDELFILE DeletedFile;
    HRESULT hr;

    if (pFI == NULL || pFI->RecycledFullPath == NULL || !RecycleBinVolumeRoot(pFI->RecycledFullPath, Root))
        return FALSE;

    DeletedFile = GetRecycleBinFileHandle(Root, pFI);
    if (DeletedFile == NULL)
        return FALSE;

    hr = IRecycleBinFile_RemoveFromDatabase(IRecycleBinFileFromHDELFILE(DeletedFile));
    CloseRecycleBinHandle(DeletedFile);
    return SUCCEEDED(hr);
}
