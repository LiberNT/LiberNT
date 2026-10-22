/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.UI.Notifications toast platform declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef WPNAPPS_PRIVATE_H
#define WPNAPPS_PRIVATE_H

#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

#define COBJMACROS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <wingdi.h>
#include <winuser.h>
#include <winstring.h>
#include <objbase.h>
#include <roapi.h>
#include <activation.h>
#include <notificationactivationcallback.h>

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include <windows.foundation.h>
#define WIDL_using_Windows_Data_Xml_Dom
#include <windows.data.xml.dom.h>
#define WIDL_using_Windows_UI_Notifications
#include <windows.ui.notifications.h>

#include <wine/list.h>

#include "identity.h"

#define TOAST_MAX_TAG_LENGTH 64

extern IActivationFactory *toast_manager_factory;
extern IActivationFactory *toast_notification_factory;
extern IActivationFactory *scheduled_toast_factory;
extern IActivationFactory *notification_data_factory;
extern HINSTANCE wpnapps_instance;

struct event_list
{
    SRWLOCK lock;
    struct list handlers;
};

void event_list_init(struct event_list *list);
void event_list_clear(struct event_list *list);
HRESULT event_list_add(struct event_list *list, void *handler, REFIID iid, EventRegistrationToken *token);
HRESULT event_list_remove(struct event_list *list, EventRegistrationToken token);
UINT32 event_list_snapshot(struct event_list *list, IUnknown ***handlers);
void event_list_release_snapshot(IUnknown **handlers, UINT32 count);

HRESULT get_iids(REFIID iid, ULONG *count, IID **iids);
HRESULT hstring_to_wide(HSTRING value, WCHAR **out);
HRESULT xml_get_string(IXmlDocument *document, WCHAR **xml);
HRESULT xml_load(const WCHAR *xml, IXmlDocument **document);

struct toast_input
{
    WCHAR *id;
    WCHAR *value;
};

HRESULT toast_create(IXmlDocument *content, IToastNotification **out);
BOOL toast_is_ours(IToastNotification *toast);
void toast_get_identity(IToastNotification *toast, WCHAR **tag, WCHAR **group);
BOOL toast_get_suppress_popup(IToastNotification *toast);
BOOL toast_get_expires_on_reboot(IToastNotification *toast);
INT64 toast_get_expiration(IToastNotification *toast);
BOOL toast_is_expired(IToastNotification *toast);
HRESULT toast_restore(IXmlDocument *content, const WCHAR *tag, const WCHAR *group, INT64 expiration,
                      BOOL suppress_popup, INotificationData *data, IToastNotification **out);
void toast_raise_dismissed(IToastNotification *toast, ToastDismissalReason reason);
void toast_raise_activated(IToastNotification *toast, const WCHAR *arguments,
                           const NOTIFICATION_USER_INPUT_DATA *inputs, ULONG input_count);
void toast_raise_failed(IToastNotification *toast, HRESULT error);
HRESULT date_time_reference_create(INT64 value, IReference_DateTime **out);

#define STORE_NAME_LENGTH 33
#define HISTORY_EVENT_DISMISSED 1
#define HISTORY_EVENT_ACTIVATED 2
#define HISTORY_EVENT_FAILED 3

struct history_record
{
    const WCHAR *xml;
    const WCHAR *tag;
    const WCHAR *group;
    INT64 expiration;
    BOOL suppress_popup;
    BOOL expires_on_reboot;
    BOOL scheduled;
    INotificationData *data;
};

HRESULT history_post(const WCHAR *app_id, IToastNotification *toast);
HRESULT history_store_post(const WCHAR *app_id, const struct history_record *record, WCHAR *name);
BOOL history_remove(IToastNotification *toast);
void history_remove_matching(const WCHAR *app_id, const WCHAR *tag, const WCHAR *group);
HRESULT history_view(const WCHAR *app_id, IVectorView_ToastNotification **view);
HRESULT history_create(IToastNotificationHistory **out);
HRESULT history_update(const WCHAR *app_id, const WCHAR *tag, const WCHAR *group, INotificationData *data,
                       NotificationUpdateResult *result);
void history_sync(void);
BOOL history_entry_exists(const WCHAR *app_id, const WCHAR *name);
void history_entry_delete(const WCHAR *app_id, const WCHAR *name);
INotificationData *history_entry_data(const WCHAR *app_id, const WCHAR *name, DWORD *revision);
IToastNotification *history_entry_restore(const WCHAR *app_id, const WCHAR *name);
void history_entry_popup_done(const WCHAR *app_id, const WCHAR *name);
void history_scan_popups(void (*callback)(const WCHAR *app_id, const WCHAR *name, void *context), void *context);
BOOL history_owner_alive(const WCHAR *name);
void history_post_event(const WCHAR *name, DWORD kind, DWORD value, const WCHAR *arguments,
                        const struct toast_input *inputs, ULONG input_count);
void history_dispatch_events(void);
void history_collect_events(void);

INT64 store_now(void);
void store_new_name(WCHAR *name);
DWORD store_name_process(const WCHAR *name);
LSTATUS store_open_area(const WCHAR *area, BOOL create, REGSAM access, HKEY *key);
LSTATUS store_open_root(HKEY *key);
LSTATUS store_open_app(const WCHAR *area, const WCHAR *app_id, BOOL create, HKEY *key);
WCHAR *store_read_string(HKEY key, const WCHAR *name);
UINT64 store_read_number(HKEY key, const WCHAR *name);
void store_write_string(HKEY key, const WCHAR *name, const WCHAR *value);
void store_write_dword(HKEY key, const WCHAR *name, DWORD value);
void store_write_qword(HKEY key, const WCHAR *name, UINT64 value);
UINT32 store_list(HKEY key, WCHAR (**names)[STORE_NAME_LENGTH]);
UINT32 store_list_apps(const WCHAR *area, WCHAR ***apps);
void store_free_apps(WCHAR **apps, UINT32 count);
HRESULT store_watch(HANDLE event, HKEY *root);

HRESULT string_map_create(IMap_HSTRING_HSTRING **out);
HRESULT notification_data_create(IIterable_IKeyValuePair_HSTRING_HSTRING *values, UINT32 sequence,
                                 INotificationData **out);

HRESULT notifier_create(const WCHAR *app_id, IToastNotifier **out);
HRESULT notifier_show(const WCHAR *app_id, IToastNotification *toast);
HRESULT notification_get_setting(const WCHAR *app_id, NotificationSetting *setting);
void notification_register_app(const WCHAR *app_id);

void presenter_run(void);
void host_ensure(void);

void activation_invoke(const WCHAR *app_id, const WCHAR *arguments, const struct toast_input *inputs,
                       ULONG input_count);

HRESULT vector_view_create(REFIID view_iid, REFIID iterable_iid, REFIID iterator_iid,
                           IInspectable **items, UINT32 count, void **out);

HRESULT scheduled_toast_get_details(IScheduledToastNotification *scheduled, IXmlDocument **content,
                                    DateTime *delivery_time, WCHAR **tag, WCHAR **group, BOOL *suppress_popup);
HRESULT scheduled_toast_get_extra(IScheduledToastNotification *scheduled, WCHAR **id, INT64 *snooze_interval,
                                  UINT32 *maximum_snooze_count, INT64 *expiration);
void scheduled_toast_set_store_key(IScheduledToastNotification *scheduled, const WCHAR *key);
BOOL scheduled_toast_get_store_key(IScheduledToastNotification *scheduled, WCHAR *key);
HRESULT scheduled_toast_restore(IXmlDocument *content, INT64 delivery_time, const WCHAR *tag, const WCHAR *group,
                                const WCHAR *id, INT64 snooze_interval, UINT32 maximum_snooze_count,
                                BOOL suppress_popup, INT64 expiration, const WCHAR *key,
                                IScheduledToastNotification **out);

HRESULT schedule_store_add(const WCHAR *app_id, IScheduledToastNotification *scheduled, WCHAR *name);
BOOL schedule_store_remove(const WCHAR *app_id, const WCHAR *name);
UINT32 schedule_store_list(const WCHAR *app_id, WCHAR (**names)[STORE_NAME_LENGTH]);
IScheduledToastNotification *schedule_store_restore(const WCHAR *app_id, const WCHAR *name);
BOOL schedule_store_claim(const WCHAR *app_id, const WCHAR *name, BOOL deliver);
void schedule_deliver(const WCHAR *app_id, IScheduledToastNotification *scheduled);
void schedule_host_scan(HWND window, UINT_PTR timer);

#endif
