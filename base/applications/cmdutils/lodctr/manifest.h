/*
 * PROJECT:     LiberNT lodctr
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Performance counter manifests (lodctr /m, unlodctr /m)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

int LodctrInstallManifest(const WCHAR *Manifest, const WCHAR *Path);
int LodctrUninstallManifest(const WCHAR *Manifest);
