/*
 * PROJECT:     LiberNT PSDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software device definitions
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#pragma once

typedef enum _SW_DEVICE_CAPABILITIES
{
    SWDeviceCapabilitiesNone           = 0x00000000,
    SWDeviceCapabilitiesRemovable      = 0x00000001,
    SWDeviceCapabilitiesSilentInstall  = 0x00000002,
    SWDeviceCapabilitiesNoDisplayInUI  = 0x00000004,
    SWDeviceCapabilitiesDriverRequired = 0x00000008
} SW_DEVICE_CAPABILITIES, *PSW_DEVICE_CAPABILITIES;

typedef struct _SW_DEVICE_CREATE_INFO
{
    ULONG cbSize;
    PCWSTR pszInstanceId;
    PCZZWSTR pszzHardwareIds;
    PCZZWSTR pszzCompatibleIds;
    const GUID *pContainerId;
    ULONG CapabilityFlags;
    PCWSTR pszDeviceDescription;
    PCWSTR pszDeviceLocation;
    const SECURITY_DESCRIPTOR *pSecurityDescriptor;
} SW_DEVICE_CREATE_INFO, *PSW_DEVICE_CREATE_INFO;

typedef enum _SW_DEVICE_LIFETIME
{
    SWDeviceLifetimeHandle,
    SWDeviceLifetimeParentPresent,
    SWDeviceLifetimeMax
} SW_DEVICE_LIFETIME, *PSW_DEVICE_LIFETIME;
