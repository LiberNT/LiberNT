/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CoreMessaging private definitions
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winstring.h"
#include "objbase.h"

#include "activation.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_System
#include "windows.foundation.h"
#include "windows.system.h"
#include "dispatcherqueue.h"

#include "wine/debug.h"
#include "wine/list.h"

#include "weakref.h"

extern IActivationFactory *dispatcher_queue_factory;
extern IActivationFactory *dispatcher_queue_controller_factory;

BOOL dispatcher_queue_process_attach(HINSTANCE instance);
void dispatcher_queue_process_detach(void);
HRESULT dispatcher_queue_controller_create(DISPATCHERQUEUE_THREAD_TYPE type,
                                           DISPATCHERQUEUE_THREAD_APARTMENTTYPE apartment,
                                           IDispatcherQueueController **out);
HRESULT dispatcher_queue_get_for_current_thread(IDispatcherQueue **out);
