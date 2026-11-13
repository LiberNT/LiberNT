/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */

/* Architecture policy for the shared C exception-raising helpers. */
#pragma once

/* The LoongArch64 CONTEXT has no debug-register file: watchpoints live in
 * CSRs, which are not part of the NT context. */
#define RTLP_RAISE_CONTEXT_FLAGS (CONTEXT_FULL | CONTEXT_UNWOUND_TO_CALL)
#define RTLP_RAISE_DEBUG_CONTEXT_FLAGS 0
