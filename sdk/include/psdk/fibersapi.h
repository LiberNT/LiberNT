/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#pragma once

/* Fiber and FLS declarations are shared with the legacy umbrella header. */
#include <winbase.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
WINBASEAPI PVOID WINAPI FlsGetValue2(DWORD dwTlsIndex);

#ifdef __cplusplus
}
#endif

