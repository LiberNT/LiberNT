/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Trace macros for the built-in debugger, printed through itself
 * COPYRIGHT:   Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef __RELFILE__
#define __RELFILE__ __FILE__
#endif

#define KDB_TRACE_DISCARD(...) do { if (0) KdbPrintf(__VA_ARGS__); } while (0)
#define KDB_TRACE(fmt, ...) KdbPrintf("(%s:%d) " fmt, __RELFILE__, __LINE__, ##__VA_ARGS__)

#if DBG
#define DPRINT1 KDB_TRACE
#else
#define DPRINT1 KDB_TRACE_DISCARD
#endif

#if DBG && !defined(NDEBUG)
#define DPRINT KDB_TRACE
#else
#define DPRINT KDB_TRACE_DISCARD
#endif
