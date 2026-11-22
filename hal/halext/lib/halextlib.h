/*
 * PROJECT:     LiberNT HAL extensions
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Binds an extension to the services table the HAL hands it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <libernt/hal/halext.h>

NTSTATUS
HalExtBindServices(
    _In_ const HAL_EXTENSION_SERVICES *Services);
