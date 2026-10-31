/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     TraceLogging provider interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <winmeta.h>

/* FIXME: TraceLogging events are not emitted; providers never register and are always disabled */

typedef const struct _TlgProvider_t *TraceLoggingHProvider;

#define TRACELOGGING_DECLARE_PROVIDER(hProvider) \
    extern const TraceLoggingHProvider hProvider

#define TRACELOGGING_DEFINE_PROVIDER(hProvider, providerName, providerId, ...) \
    const TraceLoggingHProvider hProvider = (TraceLoggingHProvider)0

#define TraceLoggingRegister(hProvider) ((HRESULT)0)
#define TraceLoggingRegisterEx(hProvider, pEnableCallback, pCallbackContext) ((HRESULT)0)
#define TraceLoggingUnregister(hProvider) ((void)0)
#define TraceLoggingProviderEnabled(hProvider, eventLevel, eventKeyword) (0)
#define TraceLoggingWrite(hProvider, eventName, ...) ((void)0)
#define TraceLoggingWriteActivity(hProvider, eventName, pActivityId, pRelatedActivityId, ...) ((void)0)
