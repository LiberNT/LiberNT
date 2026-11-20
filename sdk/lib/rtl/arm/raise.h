/*
 * PROJECT:     LiberNT Run-Time Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) policy for the shared exception-raising helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define RTLP_RAISE_EXCEPTION_NEEDS_CALLER_CONTEXT
#define RTLP_EXECUTE_HANDLER_TAKES_ROUTINE
#define RTLP_RAISE_USES_RESTORE_CONTEXT
#define RTLP_RAISE_CONTEXT_FLAGS (CONTEXT_FULL | CONTEXT_UNWOUND_TO_CALL)
#define RTLP_RAISE_DEBUG_CONTEXT_FLAGS 0
#define RTLP_RAISE_STATUS_NEEDS_CALLER_CONTEXT
