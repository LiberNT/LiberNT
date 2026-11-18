/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private registration of a processor with the kernel processor power manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <pepfx.h>

#define PPM_PROCESSOR_REQUEST_SIGNATURE 'rPpL'

typedef enum _PPM_PROCESSOR_REQUEST_TYPE
{
    PpmProcessorRegister = 1,
    PpmProcessorUnregister,
    PpmProcessorPerfConstraintsChanged,
    PpmProcessorRequestMaximum
} PPM_PROCESSOR_REQUEST_TYPE;

typedef struct _PPM_PROCESSOR_REQUEST
{
    ULONG Signature;
    ULONG Size;
    PPM_PROCESSOR_REQUEST_TYPE Type;
    PROCESSOR_NUMBER Processor;
    PEPHANDLE Handle;
    PPEPCALLBACKNOTIFYPPM AcceptProcessorNotification;
} PPM_PROCESSOR_REQUEST, *PPPM_PROCESSOR_REQUEST;
