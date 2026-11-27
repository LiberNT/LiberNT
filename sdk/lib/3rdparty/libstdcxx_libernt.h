/*
 * PROJECT:     LiberNT web engine build
 * LICENSE:     MIT (https://spdx.org/licenses/MIT.html)
 * PURPOSE:     Load libstdc++ before the SAL annotations and keep its math.h the only one
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#if defined(__GNUC__) && !defined(__clang__) && defined(__cplusplus)
#include <cstdlib>
#include <cmath>
#define _INC_MATH
#include <sal.h>
#undef __valid
#endif
