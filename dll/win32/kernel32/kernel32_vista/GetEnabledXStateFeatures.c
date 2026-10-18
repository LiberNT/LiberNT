#include "k32_vista.h"

/* Taken from Wine 11.14 kernelbase/memory.c. */

NTSYSAPI
ULONG64
NTAPI
RtlGetEnabledExtendedFeatures(
    _In_ ULONG64 FeatureMask);

/***********************************************************************
 *             GetEnabledXStateFeatures   (kernelbase.@)
 */
DWORD64 WINAPI GetEnabledXStateFeatures(void)
{
    return RtlGetEnabledExtendedFeatures(~(ULONG64)0);
}
