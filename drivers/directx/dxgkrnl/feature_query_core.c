/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WDDM feature-query result negotiation
 */

#include "feature_query_core.h"

static BOOLEAN
DxgkFeatureQueryCoreRangeValid(
    _In_ USHORT Minimum,
    _In_ USHORT Maximum)
{
    return Minimum != 0 && Maximum >= Minimum;
}

BOOLEAN
DxgkFeatureQueryCoreEvaluate(
    _In_ const DXGK_FEATURE_QUERY_CORE_INPUT *Input,
    _Out_ DXGK_FEATURE_QUERY_CORE_RESULT *Result)
{
    USHORT Minimum;
    USHORT Maximum;

    if (Input == NULL || Result == NULL)
        return FALSE;

    Result->Version = 0;
    Result->Value = 0;

    if (!Input->KnownFeature)
        return TRUE;

    Result->Value |= DXGK_FEATURE_QUERY_RESULT_KNOWN;

    if (Input->OsSupported &&
        !DxgkFeatureQueryCoreRangeValid(Input->OsMinVersion,
                                        Input->OsMaxVersion))
    {
        Result->Version = 0;
        Result->Value = 0;
        return FALSE;
    }

    if (Input->RequiresDriverSupport)
    {
        if (Input->DriverResponseValid && Input->SupportedByDriver)
        {
            if (!DxgkFeatureQueryCoreRangeValid(Input->DriverMinVersion,
                                                Input->DriverMaxVersion))
            {
                Result->Version = 0;
                Result->Value = 0;
                return FALSE;
            }

            Result->Value |=
                DXGK_FEATURE_QUERY_RESULT_SUPPORTED_BY_DRIVER;
            if (Input->SupportedOnCurrentConfig)
            {
                Result->Value |=
                    DXGK_FEATURE_QUERY_RESULT_SUPPORTED_ON_CONFIG;
            }
        }

        if (!Input->OsSupported ||
            !Input->OsSupportedOnCurrentConfig ||
            !Input->DriverResponseValid ||
            !Input->SupportedByDriver ||
            !Input->SupportedOnCurrentConfig)
        {
            return TRUE;
        }

        Minimum = Input->OsMinVersion > Input->DriverMinVersion
                      ? Input->OsMinVersion
                      : Input->DriverMinVersion;
        Maximum = Input->OsMaxVersion < Input->DriverMaxVersion
                      ? Input->OsMaxVersion
                      : Input->DriverMaxVersion;
        if (Maximum < Minimum)
            return TRUE;
    }
    else
    {
        if (Input->OsSupportedOnCurrentConfig)
        {
            Result->Value |=
                DXGK_FEATURE_QUERY_RESULT_SUPPORTED_ON_CONFIG;
        }
        if (!Input->OsSupported || !Input->OsSupportedOnCurrentConfig)
            return TRUE;

        Maximum = Input->OsMaxVersion;
    }

    Result->Version = Maximum;
    Result->Value |= DXGK_FEATURE_QUERY_RESULT_ENABLED;
    return TRUE;
}

/* Windows answers these only for an adapter that can render; HwSchEnabledByDefault is never set. */
ULONG
DxgkWddmCapsCore27(
    _In_ const DXGK_WDDM_CAPS_CORE_INPUT *Input)
{
    if (!Input->RenderCapable)
        return 0;
    return (Input->HwSchSupportState != 0 ? 0x1UL : 0) |
           (Input->HwSchEnabled ? 0x2UL : 0) |
           (Input->IndependentVidPnVSyncControl ? 0x8UL : 0);
}

ULONG
DxgkWddmCapsCore29(
    _In_ const DXGK_WDDM_CAPS_CORE_INPUT *Input)
{
    if (!Input->RenderCapable)
        return 0;
    return (Input->HwSchSupportState & 0x3UL) | (Input->HwSchEnabled ? 0x4UL : 0);
}

ULONG
DxgkWddmCapsCore30(
    _In_ const DXGK_WDDM_CAPS_CORE_INPUT *Input)
{
    if (!Input->RenderCapable)
        return 0;
    return (Input->HwFlipQueueSupportState & 0x3UL) |
           (Input->HwFlipQueueEnabled ? 0x4UL : 0) |
           (Input->DisplayableSupport ? 0x8UL : 0);
}

ULONG
DxgkWddmCapsCore31(
    _In_ const DXGK_WDDM_CAPS_CORE_INPUT *Input)
{
    return Input->RenderCapable && Input->NativeGpuFence ? 0x1UL : 0;
}

/* Windows sets ACGSupported from the driver key's ACGSupported value or for a WDDM 2.2 or later driver. */
BOOLEAN
DxgkAdapterTypeCoreAcgSupported(
    _In_ BOOLEAN AcgSupportedValue,
    _In_ ULONG WddmVersion)
{
    return AcgSupportedValue || WddmVersion >= 0x2200UL;
}

BOOLEAN
DxgkAdapterTypeCoreSetTimingsFromVidPn(
    _In_ ULONG WddmVersion,
    _In_ ULONG InterfaceVersion,
    _In_ BOOLEAN HasSetTimingsFromVidPn,
    _In_ BOOLEAN RenderCapable,
    _In_ BOOLEAN HasMultiPlaneOverlay3)
{
    return WddmVersion >= 0x2200UL && InterfaceVersion >= 0x7007UL && HasSetTimingsFromVidPn &&
           (!RenderCapable || HasMultiPlaneOverlay3);
}
