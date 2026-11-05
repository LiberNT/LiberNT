/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software device shared definitions
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#ifndef _SWDEVICEDEF_H_
#define _SWDEVICEDEF_H_

#if (NTDDI_VERSION >= NTDDI_WIN8)

#ifdef __cplusplus
extern "C" {
#endif

typedef
#ifdef MIDL_PASS
[v1_enum]
#endif
enum _SW_DEVICE_CAPABILITIES
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

typedef
#ifdef MIDL_PASS
[v1_enum]
#endif
enum _SW_DEVICE_LIFETIME
{
    SWDeviceLifetimeHandle,
    SWDeviceLifetimeParentPresent,
    SWDeviceLifetimeMax
} SW_DEVICE_LIFETIME, *PSW_DEVICE_LIFETIME;

#ifdef __cplusplus
}
#endif

#endif /* NTDDI_VERSION >= NTDDI_WIN8 */

#endif /* _SWDEVICEDEF_H_ */
