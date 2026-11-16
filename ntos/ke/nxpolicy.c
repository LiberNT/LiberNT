/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The data execution prevention policy the boot options ask for on x86 processors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

UCHAR
NTAPI
KiNxPolicyFromLoadOptions(
    _In_opt_ PCSTR LoadOptions,
    _Out_ PULONG NxFeatureBits)
{
    *NxFeatureBits = 0;

    if (LoadOptions == NULL)
        return NX_SUPPORT_POLICY_OPTIN;

    if (strstr(LoadOptions, "NOEXECUTE=ALWAYSON"))
    {
        *NxFeatureBits = KF_NX_ENABLED;
        return NX_SUPPORT_POLICY_ALWAYSON;
    }

    if (strstr(LoadOptions, "NOEXECUTE=OPTOUT"))
    {
        *NxFeatureBits = KF_NX_ENABLED;
        return NX_SUPPORT_POLICY_OPTOUT;
    }

    if (strstr(LoadOptions, "NOEXECUTE=ALWAYSOFF"))
    {
        *NxFeatureBits = KF_NX_DISABLED;
        return NX_SUPPORT_POLICY_ALWAYSOFF;
    }

    if (strstr(LoadOptions, "NOEXECUTE"))
    {
        *NxFeatureBits = KF_NX_ENABLED;
        return NX_SUPPORT_POLICY_OPTIN;
    }

    /* EXECUTE is part of NOEXECUTE, so the bare switch is only looked for once no NOEXECUTE form matched */
    if (strstr(LoadOptions, "EXECUTE"))
    {
        *NxFeatureBits = KF_NX_DISABLED;
        return NX_SUPPORT_POLICY_ALWAYSOFF;
    }

    return NX_SUPPORT_POLICY_OPTIN;
}

/* EOF */
