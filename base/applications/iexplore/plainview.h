/*
 * PROJECT:     LiberNT Web Browser
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Plain page view used when the web view is not installed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

HWND WINAPI PlainViewCreate(HWND Parent, INT Id, const LNTWEB_HOST *Host);
BOOL WINAPI PlainViewLoad(HWND View, PCSTR Html, SIZE_T Length, PCSTR Url);
