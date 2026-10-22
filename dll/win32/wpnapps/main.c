/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Activation entry points of the toast notification platform
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <initguid.h>
#include "private.h"

HINSTANCE wpnapps_instance;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        wpnapps_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (out)
        *out = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);
    IActivationFactory *source = NULL;

    if (!factory)
        return E_POINTER;
    *factory = NULL;
    if (!wcscmp(name, RuntimeClass_Windows_UI_Notifications_ToastNotificationManager))
        source = toast_manager_factory;
    else if (!wcscmp(name, RuntimeClass_Windows_UI_Notifications_ToastNotification))
        source = toast_notification_factory;
    else if (!wcscmp(name, RuntimeClass_Windows_UI_Notifications_ScheduledToastNotification))
        source = scheduled_toast_factory;
    else if (!wcscmp(name, RuntimeClass_Windows_UI_Notifications_NotificationData))
        source = notification_data_factory;
    if (!source)
        return CLASS_E_CLASSNOTAVAILABLE;
    IActivationFactory_AddRef(source);
    *factory = source;
    return S_OK;
}
