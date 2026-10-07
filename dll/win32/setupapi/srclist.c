/*
 * PROJECT:     LiberNT Setup API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Lists of installation sources of the system, the user and the process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "setupapi_private.h"

typedef struct _SOURCE_LIST
{
    PWSTR *Items;
    UINT Count;
} SOURCE_LIST, *PSOURCE_LIST;

static const WCHAR SetupKeyName[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Setup";
static const WCHAR SourcesValueName[] = L"Installation Sources";
static const WCHAR DefaultSource[] = L"A:\\";
static const PCWSTR PlatformNames[] = { L"x86", L"amd64" };

static CRITICAL_SECTION SourceListCs;
static CRITICAL_SECTION_DEBUG SourceListCsDebug =
{
    0, 0, &SourceListCs,
    { &SourceListCsDebug.ProcessLocksList, &SourceListCsDebug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": SourceListCs") }
};
static CRITICAL_SECTION SourceListCs = { &SourceListCsDebug, -1, 0, 0, 0, 0 };

static SOURCE_LIST TemporaryList;
static BOOL TemporaryListActive;
static BOOL TemporaryListNoBrowse;

static VOID
FreeList(
    _Inout_ PSOURCE_LIST List)
{
    UINT i;

    for (i = 0; i < List->Count; i++)
        MyFree(List->Items[i]);
    MyFree(List->Items);
    List->Items = NULL;
    List->Count = 0;
}

static BOOL
InsertItem(
    _Inout_ PSOURCE_LIST List,
    _In_ UINT Index,
    _In_ PCWSTR Source)
{
    PWSTR *Items, Copy;
    SIZE_T Size = (wcslen(Source) + 1) * sizeof(WCHAR);

    Copy = MyMalloc((DWORD)Size);
    Items = MyMalloc((List->Count + 1) * sizeof(PWSTR));
    if (!Copy || !Items)
    {
        MyFree(Copy);
        MyFree(Items);
        return FALSE;
    }

    CopyMemory(Copy, Source, Size);
    if (Index > List->Count)
        Index = List->Count;
    if (List->Count)
    {
        CopyMemory(Items, List->Items, Index * sizeof(PWSTR));
        CopyMemory(Items + Index + 1, List->Items + Index, (List->Count - Index) * sizeof(PWSTR));
    }
    Items[Index] = Copy;
    MyFree(List->Items);
    List->Items = Items;
    List->Count++;
    return TRUE;
}

static VOID
RemoveItem(
    _Inout_ PSOURCE_LIST List,
    _In_ UINT Index)
{
    MyFree(List->Items[Index]);
    MoveMemory(List->Items + Index, List->Items + Index + 1, (List->Count - Index - 1) * sizeof(PWSTR));
    List->Count--;
}

static BOOL
CopyList(
    _Inout_ PSOURCE_LIST Target,
    _In_ const SOURCE_LIST *Source)
{
    UINT i;

    for (i = 0; i < Source->Count; i++)
    {
        if (!InsertItem(Target, Target->Count, Source->Items[i]))
            return FALSE;
    }
    return TRUE;
}

static LONG
ReadList(
    _In_ HKEY Root,
    _Out_ PSOURCE_LIST List)
{
    PWSTR Buffer, Item;
    DWORD Type, Size = 0;
    HKEY Key;
    LONG Error;

    List->Items = NULL;
    List->Count = 0;

    Error = RegOpenKeyExW(Root, SetupKeyName, 0, KEY_QUERY_VALUE, &Key);
    if (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND)
        return ERROR_SUCCESS;
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = RegQueryValueExW(Key, SourcesValueName, NULL, &Type, NULL, &Size);
    if (Error != ERROR_SUCCESS || Type != REG_MULTI_SZ || Size < sizeof(WCHAR))
    {
        RegCloseKey(Key);
        return (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_SUCCESS) ? ERROR_SUCCESS : Error;
    }

    Buffer = MyMalloc(Size + 2 * sizeof(WCHAR));
    if (!Buffer)
    {
        RegCloseKey(Key);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    Error = RegQueryValueExW(Key, SourcesValueName, NULL, &Type, (PBYTE)Buffer, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS || Type != REG_MULTI_SZ)
    {
        MyFree(Buffer);
        return (Error == ERROR_SUCCESS) ? ERROR_SUCCESS : Error;
    }

    Buffer[Size / sizeof(WCHAR)] = UNICODE_NULL;
    Buffer[Size / sizeof(WCHAR) + 1] = UNICODE_NULL;
    for (Item = Buffer; *Item; Item += wcslen(Item) + 1)
    {
        if (!InsertItem(List, List->Count, Item))
        {
            FreeList(List);
            MyFree(Buffer);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    MyFree(Buffer);
    return ERROR_SUCCESS;
}

static LONG
WriteList(
    _In_ HKEY Root,
    _In_ const SOURCE_LIST *List)
{
    PWSTR Buffer, Item;
    SIZE_T Length = 1;
    HKEY Key;
    LONG Error;
    UINT i;

    for (i = 0; i < List->Count; i++)
        Length += wcslen(List->Items[i]) + 1;
    if (!List->Count)
        Length++;

    Buffer = MyMalloc((DWORD)(Length * sizeof(WCHAR)));
    if (!Buffer)
        return ERROR_NOT_ENOUGH_MEMORY;

    Item = Buffer;
    for (i = 0; i < List->Count; i++)
    {
        wcscpy(Item, List->Items[i]);
        Item += wcslen(Item) + 1;
    }
    if (!List->Count)
        *Item++ = UNICODE_NULL;
    *Item = UNICODE_NULL;

    Error = RegCreateKeyExW(Root, SetupKeyName, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    if (Error == ERROR_SUCCESS)
    {
        Error = RegSetValueExW(Key, SourcesValueName, 0, REG_MULTI_SZ, (const BYTE *)Buffer, (DWORD)(Length * sizeof(WCHAR)));
        RegCloseKey(Key);
    }

    MyFree(Buffer);
    return Error;
}

static BOOL
IsUnderSource(
    _In_ PCWSTR Item,
    _In_ PCWSTR Source)
{
    SIZE_T Length = wcslen(Source);

    while (Length && Source[Length - 1] == L'\\')
        Length--;
    if (_wcsnicmp(Item, Source, Length))
        return FALSE;
    return Item[Length] == UNICODE_NULL || Item[Length] == L'\\';
}

static VOID
AddToList(
    _Inout_ PSOURCE_LIST List,
    _In_ PCWSTR Source,
    _In_ BOOL Append,
    _Out_ PBOOL Complete)
{
    UINT i;

    for (i = 0; i < List->Count; )
    {
        if (!_wcsicmp(List->Items[i], Source))
            RemoveItem(List, i);
        else
            CharUpperW(List->Items[i++]);
    }

    *Complete = InsertItem(List, Append ? List->Count : 0, Source);
    if (*Complete && Append)
        CharUpperW(List->Items[List->Count - 1]);
}

static VOID
RemoveFromList(
    _Inout_ PSOURCE_LIST List,
    _In_ PCWSTR Source,
    _In_ BOOL Subdirectories)
{
    UINT i;

    for (i = 0; i < List->Count; )
    {
        if (!_wcsicmp(List->Items[i], Source) || (Subdirectories && IsUnderSource(List->Items[i], Source)))
            RemoveItem(List, i);
        else
            CharUpperW(List->Items[i++]);
    }
}

static VOID
StripPlatform(
    _Inout_ PWSTR Source)
{
    PWSTR Last = wcsrchr(Source, L'\\');
    UINT i;

    if (!Last || Last == Source)
        return;

    for (i = 0; i < _countof(PlatformNames); i++)
    {
        if (_wcsicmp(Last + 1, PlatformNames[i]))
            continue;

        if (Last == Source + 2 && Source[1] == L':')
            Last[1] = UNICODE_NULL;
        else
            *Last = UNICODE_NULL;
        return;
    }
}

static LONG
UpdateStoredList(
    _In_ HKEY Root,
    _In_ PCWSTR Source,
    _In_ BOOL Remove,
    _In_ DWORD Flags)
{
    SOURCE_LIST List;
    BOOL Complete = TRUE;
    LONG Error;

    Error = ReadList(Root, &List);
    if (Error != ERROR_SUCCESS)
        return Error;

    if (Remove)
    {
        if (!List.Count)
            return ERROR_SUCCESS;
        RemoveFromList(&List, Source, (Flags & SRCLIST_SUBDIRS) != 0);
    }
    else
    {
        AddToList(&List, Source, (Flags & SRCLIST_APPEND) != 0, &Complete);
    }

    Error = Complete ? WriteList(Root, &List) : ERROR_NOT_ENOUGH_MEMORY;
    FreeList(&List);
    return Error;
}

static BOOL
UpdateSourceList(
    _In_ DWORD Flags,
    _In_opt_ PCWSTR Source,
    _In_ BOOL Remove)
{
    BOOL Complete = TRUE;
    LONG Error = ERROR_SUCCESS;

    if (!Source)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!*Source)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    EnterCriticalSection(&SourceListCs);
    if (TemporaryListActive)
    {
        if (Remove)
            RemoveFromList(&TemporaryList, Source, (Flags & SRCLIST_SUBDIRS) != 0);
        else
            AddToList(&TemporaryList, Source, (Flags & SRCLIST_APPEND) != 0, &Complete);
        LeaveCriticalSection(&SourceListCs);

        SetLastError(Complete ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY);
        return Complete;
    }
    LeaveCriticalSection(&SourceListCs);

    if (Flags & SRCLIST_SYSIFADMIN)
        Flags |= pSetupIsUserAdmin() ? SRCLIST_SYSTEM : SRCLIST_USER;

    if (Flags & SRCLIST_SYSTEM)
        Error = UpdateStoredList(HKEY_LOCAL_MACHINE, Source, Remove, Flags);
    if (Error == ERROR_SUCCESS && (Flags & SRCLIST_USER))
        Error = UpdateStoredList(HKEY_CURRENT_USER, Source, Remove, Flags);

    SetLastError(Error);
    return Error == ERROR_SUCCESS;
}

static PSTR *
ListToMultiByte(
    _In_ PCWSTR *List,
    _In_ UINT Count)
{
    PSTR *Result;
    UINT i;

    Result = MyMalloc((Count ? Count : 1) * sizeof(PSTR));
    if (!Result)
        return NULL;

    for (i = 0; i < Count; i++)
    {
        Result[i] = pSetupUnicodeToMultiByte(List[i], CP_ACP);
        if (!Result[i])
        {
            while (i--)
                MyFree(Result[i]);
            MyFree(Result);
            return NULL;
        }
    }
    return Result;
}

BOOL
SourceListNoBrowse(VOID)
{
    BOOL NoBrowse;

    EnterCriticalSection(&SourceListCs);
    NoBrowse = TemporaryListActive && TemporaryListNoBrowse;
    LeaveCriticalSection(&SourceListCs);
    return NoBrowse;
}

/***********************************************************************
 *      SetupQuerySourceListW (SETUPAPI.@)
 */
BOOL WINAPI SetupQuerySourceListW(DWORD Flags, PCWSTR **List, PUINT Count)
{
    SOURCE_LIST Result = { NULL, 0 }, Stored;
    BOOL Complete = TRUE;
    LONG Error = ERROR_SUCCESS;
    UINT i;

    TRACE("0x%08x %p %p\n", Flags, List, Count);

    if (!List || !Count)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    EnterCriticalSection(&SourceListCs);
    if (!Flags && TemporaryListActive)
    {
        Complete = CopyList(&Result, &TemporaryList);
        LeaveCriticalSection(&SourceListCs);
    }
    else
    {
        LeaveCriticalSection(&SourceListCs);
        if (!Flags)
            Flags = SRCLIST_SYSTEM | SRCLIST_USER;

        if (Flags & SRCLIST_SYSTEM)
        {
            Error = ReadList(HKEY_LOCAL_MACHINE, &Stored);
            if (Error == ERROR_SUCCESS)
            {
                Complete = CopyList(&Result, &Stored);
                FreeList(&Stored);
            }
        }
        if (Error == ERROR_SUCCESS && Complete && (Flags & SRCLIST_USER))
        {
            Error = ReadList(HKEY_CURRENT_USER, &Stored);
            if (Error == ERROR_SUCCESS)
            {
                Complete = CopyList(&Result, &Stored);
                FreeList(&Stored);
            }
        }
    }

    if (Error == ERROR_SUCCESS && Complete && !Result.Count)
        Complete = InsertItem(&Result, 0, DefaultSource);
    if (Error == ERROR_SUCCESS && !Complete)
        Error = ERROR_NOT_ENOUGH_MEMORY;
    if (Error != ERROR_SUCCESS)
    {
        FreeList(&Result);
        SetLastError(Error);
        return FALSE;
    }

    if (!(Flags & SRCLIST_NOSTRIPPLATFORM))
    {
        for (i = 0; i < Result.Count; i++)
            StripPlatform(Result.Items[i]);
    }

    *List = (PCWSTR *)Result.Items;
    *Count = Result.Count;
    SetLastError(NO_ERROR);
    return TRUE;
}

/***********************************************************************
 *      SetupQuerySourceListA (SETUPAPI.@)
 */
BOOL WINAPI SetupQuerySourceListA(DWORD Flags, PCSTR **List, PUINT Count)
{
    PCWSTR *ListW;
    PSTR *Result;
    UINT CountW;

    TRACE("0x%08x %p %p\n", Flags, List, Count);

    if (!List || !Count)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!SetupQuerySourceListW(Flags, &ListW, &CountW))
        return FALSE;

    Result = ListToMultiByte(ListW, CountW);
    SetupFreeSourceListW(&ListW, CountW);
    if (!Result)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    *List = (PCSTR *)Result;
    *Count = CountW;
    SetLastError(NO_ERROR);
    return TRUE;
}

/***********************************************************************
 *      SetupFreeSourceListW (SETUPAPI.@)
 */
BOOL WINAPI SetupFreeSourceListW(PCWSTR **List, UINT Count)
{
    UINT i;

    TRACE("%p %u\n", List, Count);

    if (List && *List)
    {
        for (i = 0; i < Count; i++)
            MyFree((PVOID)(*List)[i]);
        MyFree((PVOID)*List);
        *List = NULL;
    }
    return TRUE;
}

/***********************************************************************
 *      SetupFreeSourceListA (SETUPAPI.@)
 */
BOOL WINAPI SetupFreeSourceListA(PCSTR **List, UINT Count)
{
    return SetupFreeSourceListW((PCWSTR **)List, Count);
}

/***********************************************************************
 *      SetupAddToSourceListW (SETUPAPI.@)
 */
BOOL WINAPI SetupAddToSourceListW(DWORD Flags, PCWSTR Source)
{
    TRACE("0x%08x %s\n", Flags, debugstr_w(Source));
    return UpdateSourceList(Flags, Source, FALSE);
}

/***********************************************************************
 *      SetupAddToSourceListA (SETUPAPI.@)
 */
BOOL WINAPI SetupAddToSourceListA(DWORD Flags, PCSTR Source)
{
    PWSTR SourceW = NULL;
    BOOL Result;
    DWORD Error;

    TRACE("0x%08x %s\n", Flags, debugstr_a(Source));

    if (Source && !(SourceW = pSetupMultiByteToUnicode(Source, CP_ACP)))
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    Result = UpdateSourceList(Flags, SourceW, FALSE);
    Error = GetLastError();
    MyFree(SourceW);
    SetLastError(Error);
    return Result;
}

/***********************************************************************
 *      SetupRemoveFromSourceListW (SETUPAPI.@)
 */
BOOL WINAPI SetupRemoveFromSourceListW(DWORD Flags, PCWSTR Source)
{
    TRACE("0x%08x %s\n", Flags, debugstr_w(Source));
    return UpdateSourceList(Flags, Source, TRUE);
}

/***********************************************************************
 *      SetupRemoveFromSourceListA (SETUPAPI.@)
 */
BOOL WINAPI SetupRemoveFromSourceListA(DWORD Flags, PCSTR Source)
{
    PWSTR SourceW = NULL;
    BOOL Result;
    DWORD Error;

    TRACE("0x%08x %s\n", Flags, debugstr_a(Source));

    if (Source && !(SourceW = pSetupMultiByteToUnicode(Source, CP_ACP)))
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    Result = UpdateSourceList(Flags, SourceW, TRUE);
    Error = GetLastError();
    MyFree(SourceW);
    SetLastError(Error);
    return Result;
}

/***********************************************************************
 *      SetupSetSourceListW (SETUPAPI.@)
 */
BOOL WINAPI SetupSetSourceListW(DWORD Flags, PCWSTR *List, UINT Count)
{
    SOURCE_LIST Replacement = { NULL, 0 };
    LONG Error;
    UINT i;

    TRACE("0x%08x %p %u\n", Flags, List, Count);

    if ((Count && !List) ||
        (!(Flags & SRCLIST_TEMPORARY) && !(Flags & SRCLIST_SYSTEM) == !(Flags & SRCLIST_USER)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    for (i = 0; i < Count; i++)
    {
        if (!List[i] || !InsertItem(&Replacement, Replacement.Count, List[i]))
        {
            FreeList(&Replacement);
            SetLastError(List[i] ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }

    if (Flags & SRCLIST_TEMPORARY)
    {
        EnterCriticalSection(&SourceListCs);
        FreeList(&TemporaryList);
        TemporaryList = Replacement;
        TemporaryListActive = TRUE;
        TemporaryListNoBrowse = (Flags & SRCLIST_NOBROWSE) != 0;
        LeaveCriticalSection(&SourceListCs);

        SetLastError(NO_ERROR);
        return TRUE;
    }

    Error = WriteList((Flags & SRCLIST_SYSTEM) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, &Replacement);
    FreeList(&Replacement);
    SetLastError(Error);
    return Error == ERROR_SUCCESS;
}

/***********************************************************************
 *      SetupSetSourceListA (SETUPAPI.@)
 */
BOOL WINAPI SetupSetSourceListA(DWORD Flags, PCSTR *List, UINT Count)
{
    PWSTR *ListW = NULL;
    BOOL Result = FALSE;
    DWORD Error = ERROR_INVALID_PARAMETER;
    UINT i, Converted = 0;

    TRACE("0x%08x %p %u\n", Flags, List, Count);

    if (Count && !List)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (Count)
    {
        ListW = MyMalloc(Count * sizeof(PWSTR));
        if (!ListW)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }

    for (i = 0; i < Count; i++, Converted++)
    {
        if (!List[i])
            break;
        ListW[i] = pSetupMultiByteToUnicode(List[i], CP_ACP);
        if (!ListW[i])
        {
            Error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
    }

    if (Converted == Count)
    {
        Result = SetupSetSourceListW(Flags, (PCWSTR *)ListW, Count);
        Error = GetLastError();
    }

    for (i = 0; i < Converted; i++)
        MyFree(ListW[i]);
    MyFree(ListW);
    SetLastError(Error);
    return Result;
}

/***********************************************************************
 *      SetupCancelTemporarySourceList (SETUPAPI.@)
 */
BOOL WINAPI SetupCancelTemporarySourceList(VOID)
{
    BOOL Active;

    EnterCriticalSection(&SourceListCs);
    Active = TemporaryListActive;
    FreeList(&TemporaryList);
    TemporaryListActive = FALSE;
    TemporaryListNoBrowse = FALSE;
    LeaveCriticalSection(&SourceListCs);

    return Active;
}
