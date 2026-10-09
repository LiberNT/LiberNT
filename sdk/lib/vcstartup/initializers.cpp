//
// section_markers.c
//
//      Copyright (c) 2024 Timo Kreuzer
//
// Markers for CRT initializer sections.
//
// SPDX-License-Identifier: MIT
//

#include <internal_shared.h>

_CRTALLOC(".CRT$XIA") _PIFV __xi_a[] = { 0 };
_CRTALLOC(".CRT$XIZ") _PIFV __xi_z[] = { 0 };
_CRTALLOC(".CRT$XCA") _PVFV __xc_a[] = { 0 };
_CRTALLOC(".CRT$XCZ") _PVFV __xc_z[] = { 0 };
_CRTALLOC(".CRT$XPA") _PVFV __xp_a[] = { 0 };
_CRTALLOC(".CRT$XPZ") _PVFV __xp_z[] = { 0 };
_CRTALLOC(".CRT$XTA") _PVFV __xt_a[] = { 0 };
_CRTALLOC(".CRT$XTZ") _PVFV __xt_z[] = { 0 };

#if defined(__GNUC__)
#include <stdlib.h>

extern "C" void __cdecl __main(void);

#if defined(__clang__)
extern "C" _PVFV __CTOR_LIST__[];
extern "C" _PVFV __DTOR_LIST__[];

static void __cdecl __do_global_dtors(void)
{
    static _PVFV* p = __DTOR_LIST__ + 1;

    while (*p)
    {
        (*p)();
        p++;
    }
}

extern "C" void __cdecl __main(void)
{
    static bool initialized;
    size_t count;

    if (initialized)
        return;
    initialized = true;

    count = (size_t)__CTOR_LIST__[0];
    if (count == (size_t)-1)
    {
        for (count = 0; __CTOR_LIST__[count + 1] != nullptr; count++)
            ;
    }

    while (count != 0)
        __CTOR_LIST__[count--]();

    atexit(__do_global_dtors);
}
#endif

_CRTALLOC(".CRT$XCU") _PVFV __scrt_gnu_main = __main;
#endif

#pragma comment(linker, "/merge:.CRT=.rdata")
