/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keep the test controller's credentials and key away from other users
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#include <windows.h>

BOOL ProtectSecretFile(const char *path);
BOOL SecretEqual(const char *given, const char *expected);
