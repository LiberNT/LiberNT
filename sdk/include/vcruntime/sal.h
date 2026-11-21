/*
 * PROJECT:     ReactOS PSDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Standard Annotation Language (SAL) definitions
 * COPYRIGHT:   2021 - Jérôme Gardou
 */

#pragma once

/* Include MS version first */
#include "ms_sal.h"

/* Some overrides with GCC attributes */
#ifdef __GNUC__
#include "gcc_sal.h"
#endif

#ifndef SAL_INCLUDED
#define SAL_INCLUDED 1
#endif
#ifndef __SAL_H_VERSION
#define __SAL_H_VERSION 180000000
#endif
