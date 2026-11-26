/*
 * PROJECT:     ReactOS runtime library (RTL)
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of RtlGetProductInfo
 * COPYRIGHT:   Copyright 2026 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

BOOLEAN
WINAPI
RtlGetProductInfo(
    _In_ ULONG OSMajorVersion,
    _In_ ULONG OSMinorVersion,
    _In_ ULONG SpMajorVersion,
    _In_ ULONG SpMinorVersion,
    _Out_ PULONG ReturnedProductType)
{
    NT_PRODUCT_TYPE ProductType;

    if (ReturnedProductType == NULL)
    {
        return FALSE;
    }

    if (OSMajorVersion < 6)
    {
        *ReturnedProductType = PRODUCT_UNDEFINED;
        return FALSE;
    }

    if (!RtlGetNtProductType(&ProductType))
    {
        *ReturnedProductType = PRODUCT_UNDEFINED;
        return FALSE;
    }

    if (ProductType == NtProductWinNt)
    {
        if ((OSMajorVersion == 6) && (OSMinorVersion == 0))
        {
            *ReturnedProductType = PRODUCT_BUSINESS;
        }
        else
        {
            *ReturnedProductType = PRODUCT_PROFESSIONAL;
        }
        return TRUE;
    }
    else if (ProductType == NtProductLanManNt)
    {
        *ReturnedProductType = PRODUCT_ENTERPRISE_SERVER;
        return TRUE;
    }
    else if (ProductType == NtProductServer)
    {
        *ReturnedProductType = PRODUCT_STANDARD_SERVER;
        return TRUE;
    }

    *ReturnedProductType = PRODUCT_UNDEFINED;
    return FALSE;
}

/* EOF */
