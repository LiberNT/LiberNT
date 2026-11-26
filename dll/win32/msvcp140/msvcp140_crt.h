/*
 * PROJECT:     LiberNT Wine DLLs
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Adapt the C runtime headers to the msvcp140 sources and tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define _Mbstatet __crt_Mbstatet
#include <share.h>
#undef _Mbstatet

typedef unsigned int (__stdcall *_beginthreadex_start_routine_t)(void *);
