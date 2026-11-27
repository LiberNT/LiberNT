/*
 * PROJECT:     LiberNT WOFF2 decoder build
 * LICENSE:     MIT (https://spdx.org/licenses/MIT.html)
 * PURPOSE:     Byte swaps woff2 expects from the CRT on Windows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>

#define _byteswap_ushort(Value) __builtin_bswap16(Value)
#define _byteswap_ulong(Value) __builtin_bswap32(Value)
