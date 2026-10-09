/*
 * FILE:            win32ss/napi.h
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PURPOSE:         System Call Table for Native API
 * PROGRAMMER:      Timo Kreuzer
 */

#define SVC_(name, argcount) (ULONG_PTR)Nt##name,
ULONG_PTR Win32kSSDT[] = {
#ifdef _WIN64
#include "w32ksvc64.h"
#else
#include "w32ksvc32.h"
#endif
};
#undef SVC_

#define SVC_(name, argcount) Win32kService##name,
enum
{
#ifdef _WIN64
#include "w32ksvc64.h"
#else
#include "w32ksvc32.h"
#endif
    Win32kServiceCount
};
#undef SVC_

#define SVC_(name, argcount) argcount * sizeof(void *),
#define SVC_FAILURE_(name, failure) [Win32kServiceCount + Win32kService##name] = failure,
UCHAR Win32kSSPT[2 * Win32kServiceCount] = {
#ifdef _WIN64
#include "w32ksvc64.h"
#else
#include "w32ksvc32.h"
#endif
#include "w32kfail.h"
};
#undef SVC_FAILURE_

#define MIN_SYSCALL_NUMBER    0x1000
#define NUMBER_OF_SYSCALLS    Win32kServiceCount
#define MAX_SYSCALL_NUMBER    0x1000 + (NUMBER_OF_SYSCALLS - 1)
ULONG Win32kNumberOfSysCalls = NUMBER_OF_SYSCALLS;
