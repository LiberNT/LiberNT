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


#define _Published_ _SAL2_Source_(_Published_, (), _SA_annotes0(SAL_externalIntf))

#define _Called_from_function_class_(x) _In_function_class_(x)

#endif
#ifndef __SAL_H_VERSION
#define __SAL_H_VERSION 180000000
#endif
