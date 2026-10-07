/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Extended processor state management
 * COPYRIGHT:   Copyright 2025 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include <ntoskrnl.h>
#include <x86x64/Cpuid.h>
#include <x86x64/Msr.h>
#define NDEBUG
#include <debug.h>

// These are not officially documented
#define XSTATE_PKRU                         9
#define XSTATE_HDC                          13
#define XSTATE_UINTR                        14
#define XSTATE_LBR                          15
#define XSTATE_MASK_PKRU                    (1LL << (XSTATE_PKRU))
#define XSTATE_MASK_HDC                     (1LL << (XSTATE_HDC))
#define XSTATE_MASK_UINTR                   (1LL << (XSTATE_UINTR))
#define XSTATE_MASK_LBR                     (1LL << (XSTATE_LBR))

#define XSTATE_MASK_SUPERVISOR \
    (XSTATE_MASK_IPT | \
     XSTATE_MASK_PASID | \
     XSTATE_MASK_CET_U | \
     XSTATE_MASK_CET_S | \
     XSTATE_MASK_HDC | \
     XSTATE_MASK_UINTR | \
     XSTATE_MASK_LBR)

/*!
 *   \brief Determines the extended state configuration for the current processor
 *
 *   \param XStateConfig - Pointer to a XSTATE_CONFIGURATION structure that receives the configuration
 *
 *   \see https://windows-internals.com/cet-on-windows/#3-xstate-configuration
 */
CODE_SEG("INIT")
static
VOID
KiGetXStateConfiguration(
    _Out_ PXSTATE_CONFIGURATION XStateConfig)
{
    ULONG64 SupportedUserMask;
    ULONG64 SupportedSupervisorMask;
    ULONG64 SupportedComponentMask;
    ULONG NextUserOffset, NextSupervisorOffset, NextOffset;

    RtlZeroMemory(XStateConfig, sizeof(*XStateConfig));

    /* Read CPUID_EXTENDED_STATE main leaf (0x0D, 0x00) */
    CPUID_EXTENDED_STATE_MAIN_LEAF_REGS ExtStateMain;
    __cpuidex(ExtStateMain.AsInt32,
              CPUID_EXTENDED_STATE,
              CPUID_EXTENDED_STATE_MAIN_LEAF);

    /* Get the supported XCR0 bits */
    SupportedUserMask = (ULONG64)ExtStateMain.Edx << 32 |
                        (ULONG64)ExtStateMain.Eax.Uint32;

    /* FIXME: Temporary workaround until we have dynamic kernel stack size */
    SupportedUserMask &= ~XSTATE_MASK_LARGE_FEATURES;

    /* Mask the allowed components */
    SupportedUserMask &= XSTATE_MASK_ALLOWED;

    /* Read CPUID_EXTENDED_STATE sub-leaf (0x0D, 0x01) */
    CPUID_EXTENDED_STATE_SUB_LEAF_REGS ExtStateSub;
    __cpuidex(ExtStateSub.AsInt32,
              CPUID_EXTENDED_STATE,
              CPUID_EXTENDED_STATE_SUB_LEAF);

    /* Save control flags */
    XStateConfig->OptimizedSave = ExtStateSub.Eax.Bits.XSAVEOPT;
    XStateConfig->CompactionEnabled = ExtStateSub.Eax.Bits.XSAVEC;
    XStateConfig->ExtendedFeatureDisable = ExtStateSub.Eax.Bits.Xfd;

    /* Determine supported supervisor features */
    SupportedSupervisorMask = 0;
    if (ExtStateSub.Eax.Bits.XSAVES)
    {
        SupportedSupervisorMask = (ULONG64)ExtStateSub.Edx << 32 |
                                  (ULONG64)ExtStateSub.Ecx.Uint32;
        SupportedSupervisorMask &= XSTATE_MASK_ALLOWED & XSTATE_MASK_SUPERVISOR;
    }

    /* Calculate full mask */
    SupportedComponentMask = SupportedUserMask | SupportedSupervisorMask;

    /* Basic features (always enabled) */
    XStateConfig->Features[XSTATE_LEGACY_FLOATING_POINT].Offset = 0;
    XStateConfig->Features[XSTATE_LEGACY_FLOATING_POINT].Size = FIELD_OFFSET(XSAVE_FORMAT, XmmRegisters);
    XStateConfig->AllFeatures[XSTATE_LEGACY_FLOATING_POINT] = FIELD_OFFSET(XSAVE_FORMAT, XmmRegisters);
    XStateConfig->Features[XSTATE_LEGACY_SSE].Offset = FIELD_OFFSET(XSAVE_FORMAT, XmmRegisters);
    XStateConfig->Features[XSTATE_LEGACY_SSE].Size = FIELD_SIZE(XSAVE_FORMAT, XmmRegisters);
    XStateConfig->AllFeatures[XSTATE_LEGACY_SSE] = FIELD_SIZE(XSAVE_FORMAT, XmmRegisters);

    /* Other components start after legacy state + header */
    NextUserOffset = NextSupervisorOffset = sizeof(XSAVE_AREA);

    /* Loop all components from 2 up */
    for (ULONG Component = 2; Component < MAXIMUM_XSTATE_FEATURES; Component++)
    {
        ULONG64 ComponentBit = (1ULL << Component);

        /* Query component features */
        CPUID_EXTENDED_STATE_SIZE_OFFSET_REGS ExtStateComponent;
        __cpuidex(ExtStateComponent.AsInt32,
                  CPUID_EXTENDED_STATE,
                  Component);

        /* Save size for all features */
        XStateConfig->AllFeatures[Component] = ExtStateComponent.Size;

        /* If the offset is 0, this component isn't valid */
        if (ExtStateComponent.Size == 0) continue;

        /* Check for components that are not OS supported */
        if ((ComponentBit & SupportedComponentMask) == 0)
        {
            /* This emulates weird (broken) Windows behavior */
            if ((ComponentBit & XSTATE_MASK_SUPERVISOR) == 0)
            {
                XStateConfig->Features[Component].Offset = ExtStateComponent.Offset;
                XStateConfig->Features[Component].Size = ExtStateComponent.Size;
            }

            /* Skip the rest */
            continue;
        }

        /* Check if compaction is enabled */
        if (XStateConfig->CompactionEnabled)
        {
            /* Align the offsets, if needed */
            if (ExtStateComponent.Ecx.Bits.Aligned)
            {
                XStateConfig->AlignedFeatures |= ComponentBit;
                NextSupervisorOffset = ALIGN_UP(NextSupervisorOffset, 64);
                if ((ComponentBit & SupportedUserMask) != 0)
                {
                    NextUserOffset = ALIGN_UP(NextUserOffset, 64);
                }
            }

            /* Update the supervisor offset */
            NextSupervisorOffset += ExtStateComponent.Size;

            /* For user components save and update the offset and size */
            if ((ComponentBit & SupportedUserMask) != 0)
            {
                XStateConfig->Features[Component].Offset = NextUserOffset;
                XStateConfig->Features[Component].Size = ExtStateComponent.Size;
                NextUserOffset += ExtStateComponent.Size;
            }
        }
        else
        {
            /* Not compacted, use the offset and size specified by the CPUID */
            NextOffset = ExtStateComponent.Offset + ExtStateComponent.Size;
            NextSupervisorOffset = max(NextSupervisorOffset, NextOffset);

            /* For user components save and update the offset and size */
            if ((ComponentBit & SupportedUserMask) != 0)
            {
                XStateConfig->Features[Component].Offset = ExtStateComponent.Offset;
                XStateConfig->Features[Component].Size = ExtStateComponent.Size;
                NextUserOffset = max(NextUserOffset, NextOffset);
            }
        }
    }

    /* Save the features to be enabled */
    XStateConfig->EnabledFeatures = SupportedUserMask;
    XStateConfig->EnabledVolatileFeatures =
        SupportedUserMask & ~XSTATE_MASK_PERSISTENT;
    XStateConfig->EnabledSupervisorFeatures = SupportedSupervisorMask;
    XStateConfig->EnabledUserVisibleSupervisorFeatures =
        SupportedSupervisorMask & XSTATE_MASK_USER_VISIBLE_SUPERVISOR;

    /* Save the calculated sizes */
    XStateConfig->Size = NextUserOffset;
    XStateConfig->AllFeatureSize = NextSupervisorOffset;
    ASSERT(XStateConfig->AllFeatureSize >= XStateConfig->Size);
}

/*!
 *   \brief Validates the provided extended state configuration against the global one
 *
 *   \param XStateConfig - Pointer to a XSTATE_CONFIGURATION structure containing the configuration
 */
CODE_SEG("INIT")
static
VOID
ValidateXStateConfig(
    _In_ PXSTATE_CONFIGURATION XState)
{
    PXSTATE_CONFIGURATION GlobalXState = &SharedUserData->XState;

    if ((XState->EnabledFeatures != GlobalXState->EnabledFeatures) ||
        (XState->EnabledSupervisorFeatures != GlobalXState->EnabledSupervisorFeatures) ||
        (XState->Size != GlobalXState->Size) ||
        (XState->AllFeatureSize != GlobalXState->AllFeatureSize))
    {
        /* Invalid features */
        KeBugCheck(MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED);
    }

    for (ULONG i = 0; i < MAXIMUM_XSTATE_FEATURES; i++)
    {
        if ((XState->Features[i].Size != GlobalXState->Features[i].Size) ||
            (XState->Features[i].Offset != GlobalXState->Features[i].Offset) ||
            (XState->AllFeatures[i] != GlobalXState->AllFeatures[i]))
        {
            /* Invalid features */
            KeBugCheck(MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED);
        }
    }
}

/*!
 *   \brief Initializes the extended state configuration for the current processor
 *
 *   \param ProcessorNumber - Number of the current processor
 */
CODE_SEG("INIT")
VOID
NTAPI
KiInitializeXStateConfiguration(
    _In_ ULONG ProcessorNumber)
{
    /* Check if XSAVE is supported */
    if ((KeFeatureBits & KF_XSTATE) == 0)
    {
        /* XSAVE is not supported */
        return;
    }

    if (ProcessorNumber == 0)
    {
        /* Processor 0: Retrieve the global configuration */
        KiGetXStateConfiguration(&SharedUserData->XState);

        if (SharedUserData->XState.AllFeatureSize == 0)
        {
            KeFeatureBits &= ~KF_XSTATE;
            return;
        }

        KeXStateLength = SharedUserData->XState.AllFeatureSize;
    }
    else
    {
        /* Processor 1+: validate the configuration against the global one */
        XSTATE_CONFIGURATION XState;
        KiGetXStateConfiguration(&XState);
        ValidateXStateConfig(&XState);
    }

    /* Enable the user mode components in XCR0 */
    _xsetbv(0, SharedUserData->XState.EnabledFeatures);

    /* Now that we have set everything up, query CPUID again to get the required
       size based on components enabled in XCR0 */
    CPUID_EXTENDED_STATE_MAIN_LEAF_REGS ExtStateMain;
    __cpuidex(ExtStateMain.AsInt32,
              CPUID_EXTENDED_STATE,
              CPUID_EXTENDED_STATE_MAIN_LEAF);

    /* CPUID 0xD, leaf 0, EBX should return the size required by all components
       enabled in XCR0 and thus match our calculation. But VBox doesn't handle
       this correctly and simply returns the full size of all *supported*
       features, independent of XCR0. We check and warn. */
    if (ExtStateMain.Ebx > SharedUserData->XState.Size)
    {
        DPRINT1("Processor %lu, CPUID 0xD, leaf 0, EBX returns 0x%x, but we calculated 0x%lx\n",
                ProcessorNumber,
                ExtStateMain.Ebx,
                SharedUserData->XState.Size);
    }

    /* Check if we have any supervisor components enabled */
    if (SharedUserData->XState.EnabledSupervisorFeatures != 0)
    {
        /* Enable the supervisor components in IA32_XSS */
        __writemsr(MSR_IA32_XSS, SharedUserData->XState.EnabledSupervisorFeatures);

        /* Get the required size for features enabled in both XCR0 and IA32_XSS */
        CPUID_EXTENDED_STATE_SUB_LEAF_REGS ExtStateSubLeaf;
        __cpuidex(ExtStateSubLeaf.AsInt32,
                  CPUID_EXTENDED_STATE,
                  CPUID_EXTENDED_STATE_SUB_LEAF);

        /* Check if all components fit into what we calculated. Same VBox issue
           here as described above. */
        if (ExtStateSubLeaf.Ebx.XSaveAreaSize > SharedUserData->XState.AllFeatureSize)
        {
            DPRINT1("Processor %lu, CPUID 0xD, leaf 1, EBX returns 0x%x, but we calculated 0x%lx\n",
                    ProcessorNumber,
                    ExtStateMain.Ebx,
                    SharedUserData->XState.Size);

            /* The problem is likely the VM, but to be safe, we adjust the size */
            SharedUserData->XState.AllFeatureSize = ExtStateSubLeaf.Ebx.XSaveAreaSize;
        }
    }
}

static
BOOLEAN
KiIsUserXStateCompacted(
    _In_ ULONG64 Layout)
{
    return SharedUserData->XState.CompactionEnabled && (Layout & XSTATE_COMPACTION_ENABLE_MASK);
}

static
ULONG
KiGetXStateStandardOffset(
    _In_ ULONG Feature)
{
    CPUID_EXTENDED_STATE_SIZE_OFFSET_REGS Component;

    __cpuidex(Component.AsInt32, CPUID_EXTENDED_STATE, Feature);
    return Component.Offset;
}

static
ULONG
KiGetUserXStateFeatureOffset(
    _In_ ULONG64 Layout,
    _In_ ULONG Feature)
{
    ULONG Offset = sizeof(XSAVE_AREA_HEADER);
    ULONG Index;

    if (!KiIsUserXStateCompacted(Layout))
        return KiGetXStateStandardOffset(Feature) - sizeof(XSAVE_FORMAT);

    for (Index = 2; Index < Feature; Index++)
    {
        if (Layout & (1ULL << Index))
            Offset += SharedUserData->XState.Features[Index].Size;
        if (SharedUserData->XState.AlignedFeatures & (1ULL << (Index + 1)))
            Offset = ALIGN_UP_BY(Offset, 64);
    }
    return Offset;
}

static
PXSAVE_AREA
KiGetXStateScratchArea(
    _In_ PVOID Scratch)
{
    return (PXSAVE_AREA)ALIGN_UP_POINTER_BY(Scratch, 64);
}

ULONG64
NTAPI
KiGetUserXStateFeatures(VOID)
{
    return SharedUserData->XState.EnabledFeatures & ~(ULONG64)XSTATE_MASK_LEGACY;
}

ULONG
NTAPI
KiGetUserXStateLength(
    _In_ ULONG64 Layout,
    _In_ ULONG64 Features)
{
    ULONG Length = sizeof(XSAVE_AREA_HEADER);
    ULONG Index, End;

    for (Index = 2; Index < MAXIMUM_XSTATE_FEATURES; Index++)
    {
        if (!(Features & (1ULL << Index)))
            continue;
        End = KiGetUserXStateFeatureOffset(Layout, Index) + SharedUserData->XState.Features[Index].Size;
        if (End > Length)
            Length = End;
    }
    return Length;
}

ULONG
NTAPI
KiGetUserXStateMaximumLength(VOID)
{
    ULONG64 Features = KiGetUserXStateFeatures();

    return KiGetUserXStateLength(Features | XSTATE_COMPACTION_ENABLE_MASK, Features);
}

ULONG
NTAPI
KiGetUserXStateScratchSize(VOID)
{
    return SharedUserData->XState.Size + 63;
}

ULONG64
NTAPI
KiGetUserXStateSetFeatures(
    _In_ PXSAVE_AREA_HEADER Header)
{
    if (KiIsUserXStateCompacted(Header->CompactionMask))
        return Header->CompactionMask & KiGetUserXStateFeatures();
    return KiGetUserXStateFeatures();
}

NTSTATUS
NTAPI
KiSaveUserXState(
    _Inout_updates_bytes_(Length) PUCHAR XState,
    _In_ ULONG Length,
    _Out_ PVOID Scratch)
{
    PXSAVE_AREA_HEADER Header = (PXSAVE_AREA_HEADER)XState;
    PXSAVE_AREA SaveArea = KiGetXStateScratchArea(Scratch);
    ULONG64 Features, Layout;
    ULONG Index;

    if (SharedUserData->XState.CompactionEnabled)
    {
        Features = Header->CompactionMask & KiGetUserXStateFeatures();
        Layout = Features | XSTATE_COMPACTION_ENABLE_MASK;
    }
    else if (Header->Mask)
    {
        Features = Header->Mask & KiGetUserXStateFeatures();
        Layout = 0;
    }
    else
    {
        Features = KiGetUserXStateFeatures();
        Layout = 0;
        for (Index = 2; Index < MAXIMUM_XSTATE_FEATURES; Index++)
        {
            if ((Features & (1ULL << Index)) &&
                (KiGetUserXStateFeatureOffset(Layout, Index) +
                 SharedUserData->XState.Features[Index].Size > Length))
            {
                Features &= ~(1ULL << Index);
            }
        }
    }

    RtlZeroMemory(SaveArea, sizeof(XSAVE_AREA));
    if (Features)
        _xsave64(SaveArea, Features);

    Header->Mask = SaveArea->Header.Mask & Features;
    Header->CompactionMask = Layout;
    RtlZeroMemory(Header->Reserved2, sizeof(Header->Reserved2));

    if (Length < KiGetUserXStateLength(Layout, Header->Mask))
        return STATUS_BUFFER_OVERFLOW;

    for (Index = 2; Index < MAXIMUM_XSTATE_FEATURES; Index++)
    {
        if (!(Header->Mask & (1ULL << Index)))
            continue;
        RtlCopyMemory(XState + KiGetUserXStateFeatureOffset(Layout, Index),
                      (PUCHAR)SaveArea + KiGetXStateStandardOffset(Index),
                      SharedUserData->XState.Features[Index].Size);
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
KiRestoreUserXState(
    _In_ PUCHAR XState,
    _Out_ PVOID Scratch)
{
    PXSAVE_AREA_HEADER Header = (PXSAVE_AREA_HEADER)XState;
    PXSAVE_AREA SaveArea = KiGetXStateScratchArea(Scratch);
    ULONG64 Features, Loaded;
    ULONG Index;

    Features = KiGetUserXStateSetFeatures(Header);
    if (!Features)
        return;
    Loaded = Header->Mask & Features;

    RtlZeroMemory(SaveArea, sizeof(XSAVE_AREA));
    _xsave64(SaveArea, Features);

    for (Index = 2; Index < MAXIMUM_XSTATE_FEATURES; Index++)
    {
        if (!(Loaded & (1ULL << Index)))
            continue;
        RtlCopyMemory((PUCHAR)SaveArea + KiGetXStateStandardOffset(Index),
                      XState + KiGetUserXStateFeatureOffset(Header->CompactionMask, Index),
                      SharedUserData->XState.Features[Index].Size);
    }

    SaveArea->Header.Mask = (SaveArea->Header.Mask & ~Features) | Loaded;
    SaveArea->Header.CompactionMask = 0;
    _xrstor64(SaveArea, Features);
}

#ifdef __clang__
__attribute__((target("fxsr")))
#endif
VOID
NTAPI
KiCaptureUserLegacyFloatingState(
    _Out_ PXSAVE_FORMAT FltSave)
{
    XSAVE_FORMAT Area;

    _fxsave64(&Area);
    FltSave->ControlWord = Area.ControlWord;
    FltSave->StatusWord = Area.StatusWord;
    FltSave->TagWord = Area.TagWord;
    FltSave->ErrorOpcode = Area.ErrorOpcode;
    FltSave->ErrorOffset = Area.ErrorOffset;
    FltSave->ErrorSelector = Area.ErrorSelector;
    FltSave->DataOffset = Area.DataOffset;
    FltSave->DataSelector = Area.DataSelector;
    FltSave->MxCsr_Mask = Area.MxCsr_Mask;
    RtlCopyMemory(FltSave->FloatRegisters, Area.FloatRegisters, sizeof(Area.FloatRegisters));
}

#define TAG_XSTATE_SAVE 'SsXK'

#ifdef __clang__
__attribute__((target("xsave,fxsr")))
#endif
NTSTATUS
NTAPI
KeSaveExtendedProcessorState(
    _In_ ULONG64 Mask,
    _Out_ PXSTATE_SAVE XStateSave)
{
    ULONG64 FeatureMask;
    ULONG Length;
    PVOID Buffer;
    PXSAVE_AREA Area;
    PKTHREAD Thread = KeGetCurrentThread();
    CPUID_EXTENDED_STATE_MAIN_LEAF_REGS ExtStateMain;

    if (KeFeatureBits & KF_XSTATE)
    {
        FeatureMask = Mask & SharedUserData->XState.EnabledFeatures;
        __cpuidex(ExtStateMain.AsInt32,
                  CPUID_EXTENDED_STATE,
                  CPUID_EXTENDED_STATE_MAIN_LEAF);
        Length = ExtStateMain.Ebx;
    }
    else
    {
        FeatureMask = Mask & XSTATE_MASK_LEGACY;
        Length = sizeof(XSAVE_FORMAT);
    }

    RtlZeroMemory(XStateSave, sizeof(*XStateSave));
    XStateSave->Thread = Thread;
    XStateSave->Level = KeGetCurrentIrql();

    if (FeatureMask == 0)
    {
        return STATUS_SUCCESS;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPoolNx, Length + XSAVE_ALIGN, TAG_XSTATE_SAVE);
    if (Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Area = ALIGN_UP_POINTER_BY(Buffer, XSAVE_ALIGN);
    RtlZeroMemory(Area, Length);

    if ((XStateSave->Level < DISPATCH_LEVEL) && (Thread->NpxState == 0))
    {
        Thread->NpxState = SharedUserData->XState.EnabledFeatures;
    }

    if (KeFeatureBits & KF_XSTATE)
    {
        _xsave64(Area, FeatureMask);
    }
    else
    {
        _fxsave64(Area);
    }

    XStateSave->XStateContext.Mask = FeatureMask;
    XStateSave->XStateContext.Length = Length;
    XStateSave->XStateContext.Area = Area;
    XStateSave->XStateContext.Buffer = Buffer;
    return STATUS_SUCCESS;
}

#ifdef __clang__
__attribute__((target("xsave,fxsr")))
#endif
VOID
NTAPI
KeRestoreExtendedProcessorState(
    _In_ PXSTATE_SAVE XStateSave)
{
    PVOID Buffer = XStateSave->XStateContext.Buffer;

    if (Buffer == NULL)
    {
        return;
    }

    if (KeFeatureBits & KF_XSTATE)
    {
        _xrstor64(XStateSave->XStateContext.Area, XStateSave->XStateContext.Mask);
    }
    else
    {
        _fxrstor64(XStateSave->XStateContext.Area);
    }

    XStateSave->XStateContext.Buffer = NULL;
    ExFreePoolWithTag(Buffer, TAG_XSTATE_SAVE);
}

ULONG64
NTAPI
RtlGetEnabledExtendedFeatures(
    _In_ ULONG64 FeatureMask)
{
    return SharedUserData->XState.EnabledFeatures & FeatureMask;
}
