/*
 * PROJECT:     LiberNT Photo Metadata Handler
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shared declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _PHOTOMETADATAHANDLER_PRECOMP_H_
#define _PHOTOMETADATAHANDLER_PRECOMP_H_

#define COBJMACROS
#define CONST_VTABLE

#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <shobjidl.h>
#include <propsys.h>

extern LONG ModuleObjects;

HRESULT ImagePropertyHandler_Create(REFIID riid, void **ppv);

#endif
