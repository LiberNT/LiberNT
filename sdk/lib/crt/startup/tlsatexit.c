/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Destructors of C++ thread_local objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <sect_attribs.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef void (__cdecl *PTHREAD_OBJECT_DTOR)(void *Object);

typedef struct _THREAD_OBJECT_DTOR
{
    struct _THREAD_OBJECT_DTOR *Next;
    PTHREAD_OBJECT_DTOR Dtor;
    void *Object;
} THREAD_OBJECT_DTOR, *PTHREAD_OBJECT_DTOR_ENTRY;

void *__dso_handle = &__dso_handle;

static LONG volatile ThreadObjectDtorSlot = (LONG)TLS_OUT_OF_INDEXES;

static DWORD
GetThreadObjectDtorSlot(void)
{
    DWORD Slot;
    DWORD NewSlot;

    Slot = (DWORD)ThreadObjectDtorSlot;
    if (Slot != TLS_OUT_OF_INDEXES)
        return Slot;

    NewSlot = TlsAlloc();
    if (NewSlot == TLS_OUT_OF_INDEXES)
        return TLS_OUT_OF_INDEXES;

    Slot = (DWORD)InterlockedCompareExchange(&ThreadObjectDtorSlot, (LONG)NewSlot, (LONG)TLS_OUT_OF_INDEXES);
    if (Slot != TLS_OUT_OF_INDEXES)
    {
        TlsFree(NewSlot);
        return Slot;
    }

    return NewSlot;
}

int __cdecl __cxa_thread_atexit(PTHREAD_OBJECT_DTOR Dtor, void *Object, void *Dso);

int __cdecl
__cxa_thread_atexit(PTHREAD_OBJECT_DTOR Dtor, void *Object, void *Dso)
{
    PTHREAD_OBJECT_DTOR_ENTRY Entry;
    DWORD LastError;
    DWORD Slot;
    int Result = -1;

    (void)Dso;

    LastError = GetLastError();
    Slot = GetThreadObjectDtorSlot();
    if (Slot != TLS_OUT_OF_INDEXES)
    {
        Entry = HeapAlloc(GetProcessHeap(), 0, sizeof(*Entry));
        if (Entry != NULL)
        {
            Entry->Dtor = Dtor;
            Entry->Object = Object;
            Entry->Next = TlsGetValue(Slot);
            if (TlsSetValue(Slot, Entry))
                Result = 0;
            else
                HeapFree(GetProcessHeap(), 0, Entry);
        }
    }
    SetLastError(LastError);
    return Result;
}

static VOID WINAPI
RunThreadObjectDtors(PVOID Module, DWORD Reason, PVOID Reserved)
{
    PTHREAD_OBJECT_DTOR_ENTRY Entry;
    DWORD Slot;

    (void)Module;
    (void)Reserved;

    if (Reason != DLL_THREAD_DETACH && Reason != DLL_PROCESS_DETACH)
        return;

    Slot = (DWORD)ThreadObjectDtorSlot;
    if (Slot == TLS_OUT_OF_INDEXES)
        return;

    while ((Entry = TlsGetValue(Slot)) != NULL)
    {
        TlsSetValue(Slot, Entry->Next);
        Entry->Dtor(Entry->Object);
        HeapFree(GetProcessHeap(), 0, Entry);
    }

    if (Reason == DLL_PROCESS_DETACH)
    {
        ThreadObjectDtorSlot = (LONG)TLS_OUT_OF_INDEXES;
        TlsFree(Slot);
    }
}

_CRTALLOC(".CRT$XLE") PIMAGE_TLS_CALLBACK __xl_e = RunThreadObjectDtors;
