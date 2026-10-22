/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Delivers toast activations to the COM activators of applications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

struct activation_work
{
    WCHAR *app_id;
    WCHAR *arguments;
    NOTIFICATION_USER_INPUT_DATA *inputs;
    ULONG input_count;
};

static void free_work(struct activation_work *work)
{
    ULONG i;

    for (i = 0; i < work->input_count; i++)
    {
        free((WCHAR *)work->inputs[i].Key);
        free((WCHAR *)work->inputs[i].Value);
    }
    free(work->inputs);
    free(work->app_id);
    free(work->arguments);
    free(work);
}

static void CALLBACK activation_callback(PTP_CALLBACK_INSTANCE instance, void *context)
{
    struct activation_work *work = context;
    INotificationActivationCallback *callback;
    struct app_identity identity;
    HRESULT init, hr;

    init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (app_identity_lookup(work->app_id, &identity) && identity.has_activator)
    {
        hr = CoCreateInstance(&identity.activator, NULL, CLSCTX_LOCAL_SERVER, &IID_INotificationActivationCallback,
                              (void **)&callback);
        if (SUCCEEDED(hr))
        {
            INotificationActivationCallback_Activate(callback, work->app_id, work->arguments ? work->arguments : L"",
                                                     work->inputs, work->input_count);
            INotificationActivationCallback_Release(callback);
        }
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    free_work(work);
}

void activation_invoke(const WCHAR *app_id, const WCHAR *arguments, const struct toast_input *inputs,
                       ULONG input_count)
{
    struct activation_work *work;
    ULONG i;

    if (!(work = calloc(1, sizeof(*work))) || !(work->app_id = wcsdup(app_id)))
    {
        free(work);
        return;
    }
    if (arguments)
        work->arguments = wcsdup(arguments);
    if (input_count && (work->inputs = calloc(input_count, sizeof(*work->inputs))))
    {
        for (i = 0; i < input_count; i++)
        {
            work->inputs[i].Key = wcsdup(inputs[i].id ? inputs[i].id : L"");
            work->inputs[i].Value = wcsdup(inputs[i].value ? inputs[i].value : L"");
        }
        work->input_count = input_count;
    }
    if (!TrySubmitThreadpoolCallback(activation_callback, work, NULL))
        free_work(work);
}
