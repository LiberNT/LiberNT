/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     POSIX clock for virglrenderer on the ReactOS CRT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include "compat.h"

int
clock_gettime(int Clock, struct timespec *Time)
{
    LARGE_INTEGER Counter, Frequency;

    if (Time == NULL)
        return -1;
    if (Clock == CLOCK_REALTIME)
        return (timespec_get(Time, TIME_UTC) == TIME_UTC) ? 0 : -1;
    if (Clock != CLOCK_MONOTONIC)
        return -1;
    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Counter);
    Time->tv_sec = (time_t)(Counter.QuadPart / Frequency.QuadPart);
    Time->tv_nsec = (long)((Counter.QuadPart % Frequency.QuadPart) * 1000000000LL / Frequency.QuadPart);
    return 0;
}

int
clock_nanosleep(int Clock, int Flags, const struct timespec *Request, struct timespec *Remain)
{
    LONG64 Milliseconds;

    (void)Clock;
    (void)Remain;
    if ((Flags != 0) || (Request == NULL))
        return -1;
    Milliseconds = (LONG64)Request->tv_sec * 1000 + Request->tv_nsec / 1000000;
    /* FIXME: sleeps shorter than a millisecond only yield the processor */
    if (Milliseconds == 0)
        SwitchToThread();
    else
        Sleep((DWORD)Milliseconds);
    return 0;
}

static BOOL CALLBACK
LxLoadVulkan(PINIT_ONCE Once, PVOID Parameter, PVOID *Context)
{
    HMODULE Loader = LoadLibraryW(L"vulkan-1.dll");

    (void)Once;
    (void)Parameter;
    *Context = (Loader != NULL) ? (PVOID)GetProcAddress(Loader, "vkGetInstanceProcAddr") : NULL;
    return TRUE;
}

void *
lxgpu_vulkan_loader(void)
{
    static INIT_ONCE Once = INIT_ONCE_STATIC_INIT;
    PVOID Proc = NULL;

    InitOnceExecuteOnce(&Once, LxLoadVulkan, NULL, &Proc);
    return Proc;
}

typedef void (*LX_VK_VOID_FUNCTION)(void);
typedef LX_VK_VOID_FUNCTION (*LX_VK_GET_INSTANCE_PROC_ADDR)(void *Instance, const char *Name);

LX_VK_VOID_FUNCTION
vkGetInstanceProcAddr(void *Instance, const char *Name)
{
    LX_VK_GET_INSTANCE_PROC_ADDR Proc = (LX_VK_GET_INSTANCE_PROC_ADDR)lxgpu_vulkan_loader();

    return (Proc != NULL) ? Proc(Instance, Name) : NULL;
}
