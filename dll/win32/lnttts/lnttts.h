/*
 * PROJECT:     LiberNT Text-to-Speech Engine
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private definitions shared by the engine parts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdarg.h>

#define COBJMACROS
#define CONST_VTABLE
#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <sapiddk.h>
#include <sperror.h>

#include "synthcore.h"

HRESULT
EngineCreate(
    _In_ REFIID Iid,
    _Outptr_ void **Object);

extern LONG EngineObjects;
