/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Installs and removes the vwifi test adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

BOOL InstallVwifi(HDEVINFO *Devices, SP_DEVINFO_DATA *Device);
VOID RemoveVwifi(HDEVINFO Devices, SP_DEVINFO_DATA *Device);
BOOL WaitInterface(HANDLE Client, GUID *Interface);
