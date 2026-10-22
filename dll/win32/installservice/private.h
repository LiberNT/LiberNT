/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.ApplicationModel.Store.Preview.InstallControl declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef INSTALLSERVICE_PRIVATE_H
#define INSTALLSERVICE_PRIVATE_H

#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

#define COBJMACROS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winstring.h>
#include <objbase.h>
#include <roapi.h>
#include <activation.h>

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include <windows.foundation.h>
#define WIDL_using_Windows_System
#include <windows.system.h>
#define WIDL_using_Windows_Management_Deployment
#include <windows.management.deployment.h>
#define WIDL_using_Windows_ApplicationModel_Store_Preview_InstallControl
#include <windows.applicationmodel.store.preview.installcontrol.h>

#include <wine/list.h>

extern IActivationFactory *app_install_manager_factory;
extern IActivationFactory *app_install_options_factory;
extern IActivationFactory *app_update_options_factory;

struct event_list
{
    SRWLOCK lock;
    struct list handlers;
};

void event_list_init(struct event_list *list);
void event_list_clear(struct event_list *list);
HRESULT event_list_add(struct event_list *list, void *handler, REFIID iid, EventRegistrationToken *token);
HRESULT event_list_remove(struct event_list *list, EventRegistrationToken token);

HRESULT get_iids(REFIID iid, ULONG *count, IID **iids);
HRESULT class_name_string(const WCHAR *name, HSTRING *class_name);
HRESULT trust_level(TrustLevel *level);

HRESULT async_boolean_create(HRESULT error, boolean value, IAsyncOperation_boolean **out);
HRESULT async_object_create(REFIID iid, HRESULT error, void **out);
HRESULT empty_install_items(IVectorView_AppInstallItem **out);

#endif
