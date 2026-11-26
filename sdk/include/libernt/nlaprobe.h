/*
 * PROJECT:     LiberNT Network Location Awareness
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Internet probe results shared by NlaSvc and the network list manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define NLA_SERVICE_KEY L"SYSTEM\\CurrentControlSet\\Services\\NlaSvc"
#define NLA_CONNECTIVITY_KEY NLA_SERVICE_KEY L"\\Connectivity"
#define NLA_CONNECTIVITY_IPV4 L"IPv4"
#define NLA_CONNECTIVITY_IPV6 L"IPv6"

typedef enum _NLA_PROBE_RESULT
{
    NlaProbeNotRun = 0,
    NlaProbeNoInternet = 1,
    NlaProbeCaptivePortal = 2,
    NlaProbeInternet = 3
} NLA_PROBE_RESULT;
