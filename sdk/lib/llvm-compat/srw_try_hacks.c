
#define _KERNEL32_

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>

#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
typedef RTL_SRWLOCK SRWLOCK, *PSRWLOCK;
#endif

#ifdef _WIN64
#define RTL_SRWLOCK_ONE 1LL
#else
#define RTL_SRWLOCK_ONE 1L
#endif

#define RTL_SRWLOCK_OWNED_BIT 0
#define RTL_SRWLOCK_CONTENDED_BIT 1
#define RTL_SRWLOCK_SHARED_BIT 2
#define RTL_SRWLOCK_CONTENTION_LOCK_BIT 3
#define RTL_SRWLOCK_OWNED (RTL_SRWLOCK_ONE << RTL_SRWLOCK_OWNED_BIT)
#define RTL_SRWLOCK_CONTENDED (RTL_SRWLOCK_ONE << RTL_SRWLOCK_CONTENDED_BIT)
#define RTL_SRWLOCK_SHARED (RTL_SRWLOCK_ONE << RTL_SRWLOCK_SHARED_BIT)
#define RTL_SRWLOCK_CONTENTION_LOCK (RTL_SRWLOCK_ONE << RTL_SRWLOCK_CONTENTION_LOCK_BIT)
#define RTL_SRWLOCK_MASK (RTL_SRWLOCK_OWNED | RTL_SRWLOCK_CONTENDED | \
                          RTL_SRWLOCK_SHARED | RTL_SRWLOCK_CONTENTION_LOCK)
#define RTL_SRWLOCK_BITS 4

BOOLEAN
WINAPI
TryAcquireSRWLockExclusive(PSRWLOCK Lock)
{
    PRTL_SRWLOCK SRWLock = (PRTL_SRWLOCK)Lock;

    return InterlockedCompareExchangePointer(&SRWLock->Ptr,
                                             (PVOID)RTL_SRWLOCK_OWNED,
                                             NULL) == NULL;
}

BOOLEAN
WINAPI
TryAcquireSRWLockShared(PSRWLOCK Lock)
{
    PRTL_SRWLOCK SRWLock = (PRTL_SRWLOCK)Lock;
    LONG_PTR CompareValue, NewValue;

    for (;;)
    {
        CompareValue = *(volatile LONG_PTR *)&SRWLock->Ptr;
        if (CompareValue == 0)
            NewValue = (RTL_SRWLOCK_ONE << RTL_SRWLOCK_BITS) | RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED;
        else if ((CompareValue & RTL_SRWLOCK_MASK) == (RTL_SRWLOCK_SHARED | RTL_SRWLOCK_OWNED))
            NewValue = CompareValue + (RTL_SRWLOCK_ONE << RTL_SRWLOCK_BITS);
        else
            return FALSE;

        if ((LONG_PTR)InterlockedCompareExchangePointer(&SRWLock->Ptr, (PVOID)NewValue, (PVOID)CompareValue) ==
            CompareValue)
        {
            return TRUE;
        }
    }
}

#ifdef _M_IX86
BOOLEAN (WINAPI *__imp_TryAcquireSRWLockExclusive)(PSRWLOCK)
    __asm__("__imp__TryAcquireSRWLockExclusive@4") = TryAcquireSRWLockExclusive;
BOOLEAN (WINAPI *__imp_TryAcquireSRWLockShared)(PSRWLOCK)
    __asm__("__imp__TryAcquireSRWLockShared@4") = TryAcquireSRWLockShared;
#else
BOOLEAN (WINAPI *__imp_TryAcquireSRWLockExclusive)(PSRWLOCK) = TryAcquireSRWLockExclusive;
BOOLEAN (WINAPI *__imp_TryAcquireSRWLockShared)(PSRWLOCK) = TryAcquireSRWLockShared;
#endif
