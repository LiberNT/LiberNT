/*
 * PROJECT:     LiberNT mbed TLS port
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows threading backend and DLL entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winbase.h>
#include <mbedtls/threading.h>

static void
MbedTlsMutexInit(mbedtls_threading_mutex_t *Mutex)
{
    if (Mutex)
        InitializeSRWLock((PSRWLOCK)&Mutex->MBEDTLS_PRIVATE(lock));
}

static void
MbedTlsMutexFree(mbedtls_threading_mutex_t *Mutex)
{
    (void)Mutex;
}

static int
MbedTlsMutexLock(mbedtls_threading_mutex_t *Mutex)
{
    if (!Mutex)
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    AcquireSRWLockExclusive((PSRWLOCK)&Mutex->MBEDTLS_PRIVATE(lock));
    return 0;
}

static int
MbedTlsMutexUnlock(mbedtls_threading_mutex_t *Mutex)
{
    if (!Mutex)
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    ReleaseSRWLockExclusive((PSRWLOCK)&Mutex->MBEDTLS_PRIVATE(lock));
    return 0;
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_ LPVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        mbedtls_threading_set_alt(MbedTlsMutexInit, MbedTlsMutexFree, MbedTlsMutexLock, MbedTlsMutexUnlock);
    }
    else if (Reason == DLL_PROCESS_DETACH && !Reserved)
    {
        mbedtls_threading_free_alt();
    }
    return TRUE;
}
