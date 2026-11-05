/*
 * PROJECT:     LiberNT Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Load configuration of a user-mode image, which holds its security cookie
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <windef.h>
#include <winnt.h>

static ULONG_PTR ImageSecurityCookie = sizeof(void *) == 8 ? (ULONG_PTR)0x00002B992DDFA232ULL : (ULONG_PTR)0xBB40E64EUL;

const IMAGE_LOAD_CONFIG_DIRECTORY _load_config_used =
{
    .Size = sizeof(IMAGE_LOAD_CONFIG_DIRECTORY),
    .SecurityCookie = (ULONG_PTR)&ImageSecurityCookie,
};
