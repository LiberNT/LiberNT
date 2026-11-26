/*
 * PROJECT:     LiberNT Network Location Awareness Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internet probe request and response handling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <libernt/nlaprobe.h>

size_t
NlaBuildProbeRequest(
    char *Buffer,
    size_t BufferSize,
    const char *Host,
    const char *Path);

NLA_PROBE_RESULT
NlaClassifyProbeResponse(
    const char *Response,
    size_t Length,
    const char *ExpectedContent);
