/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loopback HTTP proxy serving the test.winehq.org pages the http tests load
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

BOOL start_local_http_server(const WCHAR *bypass);
void stop_local_http_server(void);
