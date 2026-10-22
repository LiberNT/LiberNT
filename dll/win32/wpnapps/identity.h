/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Application user model ID resolution of the toast platform
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef WPNAPPS_IDENTITY_H
#define WPNAPPS_IDENTITY_H

struct app_identity
{
    WCHAR display_name[256];
    WCHAR icon_path[MAX_PATH];
    int icon_index;
    CLSID activator;
    BOOL has_activator;
};

BOOL app_identity_lookup(const WCHAR *app_id, struct app_identity *identity);
HRESULT current_app_id(WCHAR **app_id);

#endif
