/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Extended processor-state helpers
 * PROGRAMMERS: Wine
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

typedef struct _RTL_CONTEXT_CHUNK
{
    LONG Offset;
    ULONG Length;
} RTL_CONTEXT_CHUNK, *PRTL_CONTEXT_CHUNK;

typedef struct _RTL_CONTEXT_EX
{
    RTL_CONTEXT_CHUNK All;
    RTL_CONTEXT_CHUNK Legacy;
    RTL_CONTEXT_CHUNK XState;
} RTL_CONTEXT_EX, *PRTL_CONTEXT_EX;

/* On ARM64, XState describes the x86-compatible state and XStateArm64 the native state. */
#if defined(_ARM64_) && !defined(_M_ARM64EC)
#define NATIVE_XSTATE_CONFIGURATION (&SharedUserData->XStateArm64)
#else
#define NATIVE_XSTATE_CONFIGURATION (&SharedUserData->XState)
#endif

#ifdef __REACTOS__
ULONG64
NTAPI
RtlGetEnabledExtendedFeatures(ULONG64 FeatureMask)
{
    return NATIVE_XSTATE_CONFIGURATION->EnabledFeatures & FeatureMask;
}
#endif

static
ULONG
RtlpNextCompactedXStateOffset(ULONG Offset,
                              ULONG64 CompactionMask,
                              ULONG FeatureId,
                              const XSTATE_CONFIGURATION *Configuration)
{
    ULONG64 FeatureMask = (ULONG64)1 << FeatureId;

    if (CompactionMask & FeatureMask)
        Offset += Configuration->Features[FeatureId].Size;

    if (Configuration->AlignedFeatures & (FeatureMask << 1))
        Offset = ALIGN_UP_BY(Offset, 64);

    return Offset;
}

PVOID
WINAPI
RtlLocateExtendedFeature2(PVOID ContextExPointer,
                          ULONG FeatureId,
                          XSTATE_CONFIGURATION *XStateConfiguration,
                          PULONG Length)
{
    PRTL_CONTEXT_EX ContextEx = ContextExPointer;
    PXSAVE_AREA_HEADER XSaveHeader;
    ULONG64 FeatureMask;
    ULONG Offset, Index;

    if (XStateConfiguration != &SharedUserData->XState &&
        XStateConfiguration != NATIVE_XSTATE_CONFIGURATION)
        return NULL;

    if (FeatureId < 2 || FeatureId >= 64)
        return NULL;

    FeatureMask = (ULONG64)1 << FeatureId;
    if (!(FeatureMask & XStateConfiguration->EnabledFeatures))
        return NULL;

    XSaveHeader = (PXSAVE_AREA_HEADER)((PUCHAR)ContextEx + ContextEx->XState.Offset);

    if (Length)
        *Length = XStateConfiguration->Features[FeatureId].Size;

    if (XStateConfiguration->CompactionEnabled)
    {
        if (!(XSaveHeader->CompactionMask & FeatureMask))
            return NULL;

        Offset = sizeof(XSAVE_AREA_HEADER);
        for (Index = 2; Index < FeatureId; ++Index)
        {
            Offset = RtlpNextCompactedXStateOffset(Offset,
                                                   XSaveHeader->CompactionMask,
                                                   Index,
                                                   XStateConfiguration);
        }
    }
    else
    {
        Offset = XStateConfiguration->Features[FeatureId].Offset - sizeof(XSAVE_FORMAT);
    }

    if (Offset > ContextEx->XState.Length ||
        XStateConfiguration->Features[FeatureId].Size > ContextEx->XState.Length - Offset)
        return NULL;

    return (PUCHAR)XSaveHeader + Offset;
}

PVOID
WINAPI
RtlLocateExtendedFeature(PVOID ContextEx,
                         ULONG FeatureId,
                         PULONG Length)
{
    return RtlLocateExtendedFeature2(ContextEx,
                                     FeatureId,
                                     NATIVE_XSTATE_CONFIGURATION,
                                     Length);
}
