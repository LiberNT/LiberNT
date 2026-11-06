/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Values shared by the NDIS poll test miniport and its user-mode test
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#define NDISPOLL_HARDWARE_ID L"ROOT\\KMTESTNDISPOLL"
#define NDISPOLL_PARAMETERS_KEY L"SYSTEM\\CurrentControlSet\\Services\\ndispoll_drv\\Parameters"
#define NDISPOLL_UNSET 0xA5A5A5A5UL
#define NDISPOLL_MAX_EVENTS 32
#define NDISPOLL_DISPATCH_LEVEL 2
