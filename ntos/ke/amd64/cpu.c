/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 processor identification, features, cache geometry, control state and power hooks
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <x86x64/Cpuid.h>
#include <x86x64/Msr.h>

#define NDEBUG
#include <debug.h>

/* AMD VM_CR (SVM control); the shared MSR headers do not define it */
#define MSR_AMD_VM_CR        0xC0010114
#define AMD_VM_CR_SVMDIS     (1ULL << 4)

KTSS64 KiBootTss;

ULONG KeI386CpuType;
ULONG KeI386CpuStep;
ULONG KeI386MachineType;
ULONG KeI386NpxPresent = 1;
ULONG KeLargestCacheLine = 0x40;
ULONG KiDmaIoCoherency = 0;
BOOLEAN KiSMTProcessorsPresent;

typedef struct _KI_CPU_VENDOR
{
    CHAR Id[13];
    UCHAR Vendor;
} KI_CPU_VENDOR;

/* Some Intel parts report "GenuineIotel"; Hygon and Zhaoxin follow their licensors */
static const KI_CPU_VENDOR KiCpuVendors[] =
{
    { "GenuineIntel", CPU_INTEL },
    { "GenuineIotel", CPU_INTEL },
    { "AuthenticAMD", CPU_AMD },
    { "HygonGenuine", CPU_AMD },
    { "CentaurHauls", CPU_VIA },
    { "  Shanghai  ", CPU_VIA },
};

ULONG
NTAPI
KiGetCpuVendor(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    CPU_INFO CpuInfo;
    ULONG Index;

    KiCpuId(&CpuInfo, 0);

    /* CPUID 0 spells the vendor across EBX, EDX and ECX, in that order */
    RtlCopyMemory(&Prcb->VendorString[0], &CpuInfo.Ebx, sizeof(ULONG));
    RtlCopyMemory(&Prcb->VendorString[4], &CpuInfo.Edx, sizeof(ULONG));
    RtlCopyMemory(&Prcb->VendorString[8], &CpuInfo.Ecx, sizeof(ULONG));
    Prcb->VendorString[12] = 0;

    Prcb->CpuVendor = CPU_UNKNOWN;
    for (Index = 0; Index < RTL_NUMBER_OF(KiCpuVendors); Index++)
    {
        if (strcmp((PCHAR)Prcb->VendorString, KiCpuVendors[Index].Id) == 0)
        {
            Prcb->CpuVendor = KiCpuVendors[Index].Vendor;
            break;
        }
    }

    if (Prcb->CpuVendor == CPU_VIA)
        DPRINT1("VIA CPUs not fully supported\n");
    else if (Prcb->CpuVendor == CPU_UNKNOWN)
        DPRINT1("%s CPU support not fully tested!\n", Prcb->VendorString);

    return Prcb->CpuVendor;
}

static struct
{
    ULONG64 Aperf;
    ULONG64 Mperf;
    ULONG Mhz;
} KiAmd64ClockSample[KE_MAXIMUM_PROCESSORS];

static LONG KiAmd64AperfMperfState;

ULONG
NTAPI
KiAmd64QueryEffectiveMHz(
    _In_ ULONG ProcessorNumber)
{
    CPU_INFO CpuInfo;
    ULONG64 Aperf, Mperf, DeltaAperf, DeltaMperf;
    ULONG BaseMhz, Mhz;

    if (ProcessorNumber >= KE_MAXIMUM_PROCESSORS)
    {
        return 0;
    }

    BaseMhz = KiProcessorBlock[ProcessorNumber]->MHz;

    if (KiAmd64AperfMperfState == 0)
    {
        KiCpuId(&CpuInfo, 0);
        if (CpuInfo.Eax >= 6)
        {
            KiCpuId(&CpuInfo, 6);
            KiAmd64AperfMperfState = (CpuInfo.Ecx & 1) ? 1 : -1;
        }
        else
        {
            KiAmd64AperfMperfState = -1;
        }
    }

    if ((KiAmd64AperfMperfState < 0) || (BaseMhz == 0))
    {
        return BaseMhz;
    }

    Mperf = __readmsr(MSR_IA32_MPERF);
    Aperf = __readmsr(MSR_IA32_APERF);

    DeltaAperf = Aperf - KiAmd64ClockSample[ProcessorNumber].Aperf;
    DeltaMperf = Mperf - KiAmd64ClockSample[ProcessorNumber].Mperf;

    if (DeltaMperf < 100000ULL)
    {
        Mhz = KiAmd64ClockSample[ProcessorNumber].Mhz;
        return Mhz ? Mhz : BaseMhz;
    }

    KiAmd64ClockSample[ProcessorNumber].Aperf = Aperf;
    KiAmd64ClockSample[ProcessorNumber].Mperf = Mperf;

    Mhz = (ULONG)(((ULONG64)BaseMhz * DeltaAperf + (DeltaMperf / 2)) / DeltaMperf);
    KiAmd64ClockSample[ProcessorNumber].Mhz = Mhz;
    return Mhz;
}

VOID
NTAPI
KiSetProcessorType(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Vendor = KiGetCpuVendor();
    CPUID_VERSION_INFO_EAX Signature;
    ULONG Family, Model;
    CPU_INFO CpuInfo;

    KiCpuId(&CpuInfo, 1);
    Signature.Uint32 = CpuInfo.Eax;

    Family = Signature.Bits.FamilyId;
    Model = Signature.Bits.Model;

    /* The extended model counts on family 15, and on family 6 from Intel and Centaur */
    if ((Family == 15) || ((Family == 6) && ((Vendor == CPU_INTEL) || (Vendor == CPU_CENTAUR))))
        Model |= Signature.Bits.ExtendedModelId << 4;

    /* Capped at 0xF0 so the family sum cannot overflow its byte */
    if (Family == 15)
        Family += min(Signature.Bits.ExtendedFamilyId, 0xF0);

    Prcb->CpuID = TRUE;
    Prcb->CpuType = (UCHAR)Family;
    Prcb->CpuStep = (USHORT)((Model << 8) | Signature.Bits.SteppingId);
}

typedef enum _KI_CPUID_REGISTER
{
    KiCpuidEax,
    KiCpuidEbx,
    KiCpuidEcx,
    KiCpuidEdx
} KI_CPUID_REGISTER;

typedef struct _KI_FEATURE_RULE
{
    ULONG Leaf;
    ULONG SubLeaf;
    UCHAR Register;
    UCHAR Bit;
    UCHAR Vendor;
    ULONG64 Features;
} KI_FEATURE_RULE;

/* Bit positions are those of the CPUID structures in x86x64/Intel/Cpuid.h and x86x64/Amd/Cpuid.h */
static const KI_FEATURE_RULE KiFeatureRules[] =
{
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 1, CPU_UNKNOWN, KF_CR4 },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 3, CPU_UNKNOWN, KF_LARGE_PAGE | KF_CR4 },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 4, CPU_UNKNOWN, KF_RDTSC },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 8, CPU_UNKNOWN, KF_CMPXCHG8B },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 11, CPU_UNKNOWN, KF_FAST_SYSCALL },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 12, CPU_UNKNOWN, KF_MTRR },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 13, CPU_UNKNOWN, KF_GLOBAL_PAGE | KF_CR4 },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 15, CPU_UNKNOWN, KF_CMOV },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 16, CPU_UNKNOWN, KF_PAT },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 21, CPU_UNKNOWN, KF_DTS },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 23, CPU_UNKNOWN, KF_MMX },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 24, CPU_UNKNOWN, KF_FXSR },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 25, CPU_UNKNOWN, KF_XMMI },
    { CPUID_VERSION_INFO, 0, KiCpuidEdx, 26, CPU_UNKNOWN, KF_XMMI64 },
    { CPUID_VERSION_INFO, 0, KiCpuidEcx, 0, CPU_UNKNOWN, KF_SSE3 },
    { CPUID_VERSION_INFO, 0, KiCpuidEcx, 9, CPU_UNKNOWN, KF_SSSE3 },
    { CPUID_VERSION_INFO, 0, KiCpuidEcx, 13, CPU_UNKNOWN, KF_CMPXCHG16B },
    { CPUID_VERSION_INFO, 0, KiCpuidEcx, 19, CPU_UNKNOWN, KF_SSE4_1 },
    { CPUID_VERSION_INFO, 0, KiCpuidEcx, 20, CPU_UNKNOWN, KF_SSE4_2 },
    { CPUID_VERSION_INFO, 0, KiCpuidEcx, 26, CPU_UNKNOWN, KF_XSTATE },
    { CPUID_VERSION_INFO, 0, KiCpuidEcx, 28, CPU_UNKNOWN, KF_AVX },
    { CPUID_VERSION_INFO, 0, KiCpuidEcx, 30, CPU_UNKNOWN, KF_RDRAND },
    { CPUID_THERMAL_POWER_MANAGEMENT, 0, KiCpuidEcx, 1, CPU_UNKNOWN, KF_ACNT2 },
    { CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS, CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_SUB_LEAF_INFO, KiCpuidEbx, 0, CPU_UNKNOWN, KF_RDWRFSGSBASE },
    { CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS, CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_SUB_LEAF_INFO, KiCpuidEbx, 5, CPU_UNKNOWN, KF_AVX2 },
    { CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS, CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_SUB_LEAF_INFO, KiCpuidEbx, 7, CPU_UNKNOWN, KF_SMEP },
    { CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS, CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_SUB_LEAF_INFO, KiCpuidEbx, 16, CPU_UNKNOWN, KF_AVX512F },
    { CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS, CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_SUB_LEAF_INFO, KiCpuidEbx, 20, CPU_UNKNOWN, KF_SMAP },
    { CPUID_EXTENDED_STATE, CPUID_EXTENDED_STATE_SUB_LEAF, KiCpuidEax, 0, CPU_UNKNOWN, KF_XSAVEOPT },
    { CPUID_EXTENDED_STATE, CPUID_EXTENDED_STATE_SUB_LEAF, KiCpuidEax, 3, CPU_UNKNOWN, KF_XSAVES },
    { CPUID_EXTENDED_CPU_SIG, 0, KiCpuidEdx, 20, CPU_UNKNOWN, KF_NX_BIT },
    { CPUID_EXTENDED_CPU_SIG, 0, KiCpuidEdx, 26, CPU_UNKNOWN, KF_HUGEPAGE },
    { CPUID_EXTENDED_CPU_SIG, 0, KiCpuidEdx, 27, CPU_UNKNOWN, KF_RDTSCP },
    { CPUID_EXTENDED_CPU_SIG, 0, KiCpuidEdx, 31, CPU_AMD, KF_3DNOW },
};

static
BOOLEAN
KiCpuidLeafPresent(
    _In_ ULONG Leaf,
    _In_ ULONG MaxBasic,
    _In_ ULONG MaxExtended)
{
    if (Leaf >= CPUID_EXTENDED_FUNCTION)
        return (BOOLEAN)(((MaxExtended & 0xFFFFFF00) == CPUID_EXTENDED_FUNCTION) && (MaxExtended >= Leaf));

    /* Leaf 1 exists on anything that identifies its vendor */
    return (BOOLEAN)((Leaf == CPUID_VERSION_INFO) || (MaxBasic >= Leaf));
}

static
ULONG64
KiFeaturesFromRules(
    _In_ ULONG Vendor,
    _In_ ULONG MaxBasic,
    _In_ ULONG MaxExtended)
{
    ULONG64 Features = 0;
    INT32 Registers[4] = { 0 };
    ULONG CachedLeaf = MAXULONG, CachedSubLeaf = MAXULONG;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(KiFeatureRules); Index++)
    {
        const KI_FEATURE_RULE *Rule = &KiFeatureRules[Index];

        if ((Rule->Vendor != CPU_UNKNOWN) && (Rule->Vendor != Vendor))
            continue;

        if (!KiCpuidLeafPresent(Rule->Leaf, MaxBasic, MaxExtended))
            continue;

        if ((Rule->Leaf != CachedLeaf) || (Rule->SubLeaf != CachedSubLeaf))
        {
            __cpuidex(Registers, Rule->Leaf, Rule->SubLeaf);
            CachedLeaf = Rule->Leaf;
            CachedSubLeaf = Rule->SubLeaf;
        }

        if ((ULONG)Registers[Rule->Register] & (1UL << Rule->Bit))
            Features |= Rule->Features;
    }

    return Features;
}

static
ULONG64
KiIntelOnlyFeatures(
    _In_ const CPUID_VERSION_INFO_REGS *VersionInfo)
{
    ULONG64 Features = KF_GENUINE_INTEL;

    if ((VersionInfo->Eax.Bits.FamilyId == 6) &&
        ((VersionInfo->Eax.Bits.Model == 15) || (VersionInfo->Eax.Bits.Model == 22) ||
         (VersionInfo->Eax.Bits.Model == 23) || (VersionInfo->Eax.Bits.Model == 26)))
    {
        Features |= KF_BRANCH;
    }

    if (VersionInfo->Ecx.Bits.VMX)
    {
        MSR_IA32_VMX_PROCBASED_CTLS_REGISTER Controls;
        MSR_IA32_FEATURE_CONTROL_REGISTER FeatureControl;

        Controls.Uint64 = __readmsr(MSR_IA32_VMX_PROCBASED_CTLS);
        if (Controls.Bits.Allowed1.ActivateSecondaryControls)
        {
            MSR_IA32_VMX_PROCBASED_CTLS2_REGISTER Secondary;

            Secondary.Uint64 = __readmsr(MSR_IA32_VMX_PROCBASED_CTLS2);
            if (Secondary.Bits.Allowed1.EPT)
                Features |= KF_SLAT;
        }

        /* VMX is usable while IA32_FEATURE_CONTROL is unlocked, or locked with VMXON enabled */
        FeatureControl.Uint64 = __readmsr(MSR_IA32_FEATURE_CONTROL);
        if (!FeatureControl.Bits.Lock ||
            FeatureControl.Bits.EnableVmxInsideSmx ||
            FeatureControl.Bits.EnableVmxOutsideSmx)
        {
            Features |= KF_VIRT_FIRMWARE_ENABLED;
        }
    }

    return Features;
}

static
ULONG64
KiAmdOnlyFeatures(
    _In_ ULONG MaxExtended)
{
    ULONG64 Features = KF_AUTHENTICAMD | KF_BRANCH;

    if (KiCpuidLeafPresent(CPUID_AMD_SVM_FEATURES, 0, MaxExtended))
    {
        CPUID_AMD_SVM_FEATURES_REGS Svm;

        __cpuid(Svm.AsInt32, CPUID_AMD_SVM_FEATURES);
        if (Svm.Edx.Bits.NP)
            Features |= KF_SLAT;
    }

    if (KiCpuidLeafPresent(CPUID_EXTENDED_CPU_SIG, 0, MaxExtended))
    {
        CPUID_EXTENDED_CPU_SIG_REGS Signature;

        __cpuid(Signature.AsInt32, CPUID_EXTENDED_CPU_SIG);

        /* Firmware leaves SVM enabled unless VM_CR.SVMDIS is set */
        if (Signature.Amd.Ecx.Bits.SVM && !(__readmsr(MSR_AMD_VM_CR) & AMD_VM_CR_SVMDIS))
            Features |= KF_VIRT_FIRMWARE_ENABLED;
    }

    return Features;
}

ULONG64
NTAPI
KiGetFeatureBits(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Vendor = Prcb->CpuVendor;
    CPUID_SIGNATURE_REGS Signature;
    CPUID_VERSION_INFO_REGS VersionInfo;
    CPUID_EXTENDED_FUNCTION_REGS Extended;
    ULONG64 Features;

    if (Vendor == CPU_UNKNOWN)
        return 0;

    __cpuid(Signature.AsInt32, CPUID_SIGNATURE);
    __cpuid(VersionInfo.AsInt32, CPUID_VERSION_INFO);
    __cpuid(Extended.AsInt32, CPUID_EXTENDED_FUNCTION);

    Prcb->InitialApicId = (UCHAR)VersionInfo.Ebx.Bits.InitialLocalApicId;

    if (VersionInfo.Edx.Bits.HTT)
    {
        Prcb->LogicalProcessorsPerCore = VersionInfo.Ebx.Bits.MaximumAddressableIdsForLogicalProcessors;
        if (Prcb->LogicalProcessorsPerCore > 1)
            KiSMTProcessorsPresent = TRUE;
    }
    else
    {
        Prcb->LogicalProcessorsPerCore = 1;
    }

    Features = KiFeaturesFromRules(Vendor, Signature.MaxLeaf, Extended.MaxLeaf);

    if (Vendor == CPU_INTEL)
        Features |= KiIntelOnlyFeatures(&VersionInfo);
    else if (Vendor == CPU_AMD)
        Features |= KiAmdOnlyFeatures(Extended.MaxLeaf);

    return Features;
}

#if DBG
typedef struct _KI_NAMED_BIT
{
    ULONG64 Bit;
    PCSTR Name;
} KI_NAMED_BIT;

#define KI_NAMED(Bit) { (Bit), #Bit }

static const KI_NAMED_BIT KiKernelFeatureNames[] =
{
    KI_NAMED(KF_SMEP),
    KI_NAMED(KF_RDTSC),
    KI_NAMED(KF_CR4),
    KI_NAMED(KF_CMOV),
    KI_NAMED(KF_GLOBAL_PAGE),
    KI_NAMED(KF_LARGE_PAGE),
    KI_NAMED(KF_MTRR),
    KI_NAMED(KF_CMPXCHG8B),
    KI_NAMED(KF_MMX),
    KI_NAMED(KF_DTS),
    KI_NAMED(KF_PAT),
    KI_NAMED(KF_FXSR),
    KI_NAMED(KF_FAST_SYSCALL),
    KI_NAMED(KF_XMMI),
    KI_NAMED(KF_3DNOW),
    KI_NAMED(KF_XSAVEOPT),
    KI_NAMED(KF_XMMI64),
    KI_NAMED(KF_BRANCH),
    KI_NAMED(KF_00040000),
    KI_NAMED(KF_SSE3),
    KI_NAMED(KF_CMPXCHG16B),
    KI_NAMED(KF_AUTHENTICAMD),
    KI_NAMED(KF_ACNT2),
    KI_NAMED(KF_XSTATE),
    KI_NAMED(KF_GENUINE_INTEL),
    KI_NAMED(KF_SLAT),
    KI_NAMED(KF_VIRT_FIRMWARE_ENABLED),
    KI_NAMED(KF_RDWRFSGSBASE),
    KI_NAMED(KF_NX_BIT),
    KI_NAMED(KF_NX_DISABLED),
    KI_NAMED(KF_NX_ENABLED),
    KI_NAMED(KF_RDRAND),
    KI_NAMED(KF_SMAP),
    KI_NAMED(KF_RDTSCP),
    KI_NAMED(KF_HUGEPAGE),
    KI_NAMED(KF_XSAVES),
    KI_NAMED(KF_FPU_LEAKAGE),
    KI_NAMED(KF_CAT),
    KI_NAMED(KF_CET_SS),
    KI_NAMED(KF_SSSE3),
    KI_NAMED(KF_SSE4_1),
    KI_NAMED(KF_SSE4_2),
    KI_NAMED(KF_AVX),
    KI_NAMED(KF_AVX2),
    KI_NAMED(KF_AVX512F),
};

static const KI_NAMED_BIT KiCpuidFeatureNames[] =
{
    KI_NAMED(X86_FEATURE_PAE),
    KI_NAMED(X86_FEATURE_HT),
};

static
VOID
KiAppendNamedBits(
    _Inout_updates_bytes_(Size) PCHAR Line,
    _In_ SIZE_T Size,
    _In_ ULONG64 Bits,
    _In_reads_(Count) const KI_NAMED_BIT *Names,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Bits & Names[Index].Bit)
        {
            RtlStringCbCatA(Line, Size, " ");
            RtlStringCbCatA(Line, Size, Names[Index].Name);
        }
    }
}

VOID
KiReportCpuFeatures(
    _In_ PKPRCB Prcb)
{
    ULONG64 FeatureBits = Prcb->FeatureBits | ((ULONG64)Prcb->FeatureBitsHigh << 32);
    ULONG CpuidEdx = 0;
    CHAR Line[1024] = "Supported CPU features:";
    CPU_INFO CpuInfo;

    if (Prcb->CpuVendor)
    {
        KiCpuId(&CpuInfo, 1);
        CpuidEdx = CpuInfo.Edx;
    }

    KiAppendNamedBits(Line, sizeof(Line), FeatureBits, KiKernelFeatureNames, RTL_NUMBER_OF(KiKernelFeatureNames));
    KiAppendNamedBits(Line, sizeof(Line), CpuidEdx, KiCpuidFeatureNames, RTL_NUMBER_OF(KiCpuidFeatureNames));
    DPRINT1("%s\n", Line);
}
#endif

static
ULONG
KiIntelSecondLevelCacheSize(VOID)
{
    CPUID_CACHE_PARAMS_EAX Eax;
    CPUID_CACHE_PARAMS_EBX Ebx;
    ULONG SubLeaf, Sets;
    INT32 Registers[4];

    /* Leaf 4 lists each cache level, terminated by a null type */
    for (SubLeaf = 0;; SubLeaf++)
    {
        __cpuidex(Registers, CPUID_CACHE_PARAMS, SubLeaf);
        Eax.Uint32 = (ULONG)Registers[0];
        Ebx.Uint32 = (ULONG)Registers[1];
        Sets = (ULONG)Registers[2];

        if (Eax.Bits.CacheType == CPUID_CACHE_PARAMS_CACHE_TYPE_NULL)
            return 0;

        if ((Eax.Bits.CacheLevel == 2) && (Eax.Bits.CacheType != CPUID_CACHE_PARAMS_CACHE_TYPE_INSTRUCTION))
        {
            return (Ebx.Bits.Ways + 1) * (Ebx.Bits.LinePartitions + 1) *
                   (Ebx.Bits.LineSize + 1) * (Sets + 1);
        }
    }
}

static
ULONG
KiAmdSecondLevelCacheSize(VOID)
{
    CPU_INFO CpuInfo;

    KiCpuId(&CpuInfo, 0x80000000);
    if (CpuInfo.Eax < 0x80000006)
        return 0;

    /* ECX[31:16] is the L2 size in KB */
    KiCpuId(&CpuInfo, 0x80000006);
    return (CpuInfo.Ecx >> 16) * 1024;
}

VOID
NTAPI
KiGetCacheInformation(VOID)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    CPU_INFO CpuInfo;

    switch (KiGetCpuVendor())
    {
        case CPU_INTEL:
            KiCpuId(&CpuInfo, 0);
            Pcr->SecondLevelCacheSize = (CpuInfo.Eax >= CPUID_CACHE_PARAMS) ? KiIntelSecondLevelCacheSize() : 0;
            break;

        case CPU_AMD:
            Pcr->SecondLevelCacheSize = KiAmdSecondLevelCacheSize();
            break;

        default:
            Pcr->SecondLevelCacheSize = 0;
            break;
    }
}

VOID
NTAPI
KiRestoreProcessorControlState(
    PKPROCESSOR_STATE ProcessorState)
{
    __writecr0(ProcessorState->SpecialRegisters.Cr0);

    __writecr3(ProcessorState->SpecialRegisters.Cr3);
    __writecr4(ProcessorState->SpecialRegisters.Cr4);
    __writecr8(ProcessorState->SpecialRegisters.Cr8);

    __writedr(0, ProcessorState->SpecialRegisters.KernelDr0);
    __writedr(1, ProcessorState->SpecialRegisters.KernelDr1);
    __writedr(2, ProcessorState->SpecialRegisters.KernelDr2);
    __writedr(3, ProcessorState->SpecialRegisters.KernelDr3);
    __writedr(6, ProcessorState->SpecialRegisters.KernelDr6);
    __writedr(7, ProcessorState->SpecialRegisters.KernelDr7);

    __lgdt(&ProcessorState->SpecialRegisters.Gdtr.Limit);

    __lidt(&ProcessorState->SpecialRegisters.Idtr.Limit);

    _mm_setcsr(ProcessorState->SpecialRegisters.MxCsr);

    __writemsr(X86_MSR_GSBASE, ProcessorState->SpecialRegisters.MsrGsBase);
    __writemsr(X86_MSR_KERNEL_GSBASE, ProcessorState->SpecialRegisters.MsrGsSwap);
    __writemsr(X86_MSR_STAR, ProcessorState->SpecialRegisters.MsrStar);
    __writemsr(X86_MSR_LSTAR, ProcessorState->SpecialRegisters.MsrLStar);
    __writemsr(X86_MSR_CSTAR, ProcessorState->SpecialRegisters.MsrCStar);
    __writemsr(X86_MSR_SFMASK, ProcessorState->SpecialRegisters.MsrSyscallMask);
}

VOID
NTAPI
KiSaveProcessorControlState(
    _Out_ PKPROCESSOR_STATE ProcessorState)
{
    ProcessorState->SpecialRegisters.Cr0 = __readcr0();
    ProcessorState->SpecialRegisters.Cr2 = __readcr2();
    ProcessorState->SpecialRegisters.Cr3 = __readcr3();
    ProcessorState->SpecialRegisters.Cr4 = __readcr4();
    ProcessorState->SpecialRegisters.Cr8 = __readcr8();

    ProcessorState->SpecialRegisters.KernelDr0 = __readdr(0);
    ProcessorState->SpecialRegisters.KernelDr1 = __readdr(1);
    ProcessorState->SpecialRegisters.KernelDr2 = __readdr(2);
    ProcessorState->SpecialRegisters.KernelDr3 = __readdr(3);
    ProcessorState->SpecialRegisters.KernelDr6 = __readdr(6);
    ProcessorState->SpecialRegisters.KernelDr7 = __readdr(7);

    __sgdt(&ProcessorState->SpecialRegisters.Gdtr.Limit);
    __sldt(&ProcessorState->SpecialRegisters.Ldtr);
    __str(&ProcessorState->SpecialRegisters.Tr);
    __sidt(&ProcessorState->SpecialRegisters.Idtr.Limit);

    ProcessorState->SpecialRegisters.MxCsr = _mm_getcsr();

    ProcessorState->SpecialRegisters.MsrGsBase = __readmsr(X86_MSR_GSBASE);
    ProcessorState->SpecialRegisters.MsrGsSwap = __readmsr(X86_MSR_KERNEL_GSBASE);
    ProcessorState->SpecialRegisters.MsrStar = __readmsr(X86_MSR_STAR);
    ProcessorState->SpecialRegisters.MsrLStar = __readmsr(X86_MSR_LSTAR);
    ProcessorState->SpecialRegisters.MsrCStar = __readmsr(X86_MSR_CSTAR);
    ProcessorState->SpecialRegisters.MsrSyscallMask = __readmsr(X86_MSR_SFMASK);
}

VOID
NTAPI
KiSaveProcessorState(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    Prcb->ProcessorState.ContextFrame.ContextFlags = CONTEXT_ALL;
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Prcb->ProcessorState.ContextFrame);

    KiSaveProcessorControlState(&Prcb->ProcessorState);

    /* Kernel frames keep debug slots only for some vectors, so the control state is authoritative for them */
    if (TrapFrame->PreviousMode == KernelMode)
    {
        Prcb->ProcessorState.ContextFrame.Dr0 = Prcb->ProcessorState.SpecialRegisters.KernelDr0;
        Prcb->ProcessorState.ContextFrame.Dr1 = Prcb->ProcessorState.SpecialRegisters.KernelDr1;
        Prcb->ProcessorState.ContextFrame.Dr2 = Prcb->ProcessorState.SpecialRegisters.KernelDr2;
        Prcb->ProcessorState.ContextFrame.Dr3 = Prcb->ProcessorState.SpecialRegisters.KernelDr3;
        Prcb->ProcessorState.ContextFrame.Dr6 = Prcb->ProcessorState.SpecialRegisters.KernelDr6;
        Prcb->ProcessorState.ContextFrame.Dr7 = Prcb->ProcessorState.SpecialRegisters.KernelDr7;
    }
}

VOID
NTAPI
KiRestoreProcessorState(
    _Out_ PKTRAP_FRAME TrapFrame,
    _Out_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    KeContextToTrapFrame(&Prcb->ProcessorState.ContextFrame,
                         ExceptionFrame,
                         TrapFrame,
                         CONTEXT_ALL,
                         TrapFrame->PreviousMode);

    /* Keep any debugger edits made to the frozen processor's context */
    if (TrapFrame->PreviousMode == KernelMode)
    {
        Prcb->ProcessorState.SpecialRegisters.KernelDr0 = Prcb->ProcessorState.ContextFrame.Dr0;
        Prcb->ProcessorState.SpecialRegisters.KernelDr1 = Prcb->ProcessorState.ContextFrame.Dr1;
        Prcb->ProcessorState.SpecialRegisters.KernelDr2 = Prcb->ProcessorState.ContextFrame.Dr2;
        Prcb->ProcessorState.SpecialRegisters.KernelDr3 = Prcb->ProcessorState.ContextFrame.Dr3;
        Prcb->ProcessorState.SpecialRegisters.KernelDr6 = Prcb->ProcessorState.ContextFrame.Dr6;
        Prcb->ProcessorState.SpecialRegisters.KernelDr7 = Prcb->ProcessorState.ContextFrame.Dr7;
    }

    KiRestoreProcessorControlState(&Prcb->ProcessorState);
}

NTSTATUS
NTAPI
KxSaveFloatingPointState(
    _Out_ PKFLOATING_SAVE FloatingState)
{
    UNREFERENCED_PARAMETER(FloatingState);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KxRestoreFloatingPointState(
    _In_ PKFLOATING_SAVE FloatingState)
{
    UNREFERENCED_PARAMETER(FloatingState);
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    __wbinvd();
    return TRUE;
}

VOID
FASTCALL
KeInvalidateRangeAllCaches(
    _In_ PVOID BaseAddress,
    _In_ ULONG Length)
{
    CPU_INFO CpuInfo;
    ULONG_PTR Address, EndAddress;
    ULONG CacheLineSize;

    if (Length == 0)
        return;

    CacheLineSize = KeLargestCacheLine ? KeLargestCacheLine : 64;
    Address = (ULONG_PTR)BaseAddress & ~((ULONG_PTR)CacheLineSize - 1);
    EndAddress = (ULONG_PTR)BaseAddress + Length;

    KiCpuId(&CpuInfo, 1);
    if (!(CpuInfo.Edx & (1UL << 19)))
    {
        __wbinvd();
        return;
    }

    _mm_mfence();
    do
    {
        _mm_clflush((PVOID)Address);
        Address += CacheLineSize;
    } while ((Address < EndAddress) && (Address != 0));
    _mm_mfence();
}

ULONG
NTAPI
KeGetRecommendedSharedDataAlignment(VOID)
{
    return KeLargestCacheLine;
}

VOID
NTAPI
KeSetDmaIoCoherency(
    _In_ ULONG Coherency)
{
    KiDmaIoCoherency = Coherency;
}

#undef KeFlushIoBuffers
VOID
NTAPI
KeFlushIoBuffers(
    _Inout_ PMDL Mdl,
    _In_ BOOLEAN ReadOperation,
    _In_ BOOLEAN DmaOperation)
{
    /* x86-64 DMA is cache coherent, so there is nothing to flush */
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(ReadOperation);
    UNREFERENCED_PARAMETER(DmaOperation);
}

VOID
KiDisplayBugCheckRegisterState(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context)
{
    CHAR Line[128];

    if (TrapFrame != NULL)
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "RIP=%016I64x RSP=%016I64x RBP=%016I64x EFL=%08lx\r\n", TrapFrame->Rip, TrapFrame->Rsp, TrapFrame->Rbp, TrapFrame->EFlags);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RAX=%016I64x RBX=%016I64x RCX=%016I64x\r\n", TrapFrame->Rax, TrapFrame->Rbx, TrapFrame->Rcx);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RDX=%016I64x RSI=%016I64x RDI=%016I64x\r\n", TrapFrame->Rdx, TrapFrame->Rsi, TrapFrame->Rdi);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R8 =%016I64x R9 =%016I64x R10=%016I64x\r\n", TrapFrame->R8, TrapFrame->R9, TrapFrame->R10);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R11=%016I64x FAR=%016I64x ERR=%016I64x\r\n", TrapFrame->R11, TrapFrame->FaultAddress, TrapFrame->ErrorCode);
        KiDisplayAndLogBugCheckString(Line);
    }
    else
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "RIP=%016I64x RSP=%016I64x RBP=%016I64x EFL=%08lx\r\n", Context->Rip, Context->Rsp, Context->Rbp, Context->EFlags);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RAX=%016I64x RBX=%016I64x RCX=%016I64x\r\n", Context->Rax, Context->Rbx, Context->Rcx);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RDX=%016I64x RSI=%016I64x RDI=%016I64x\r\n", Context->Rdx, Context->Rsi, Context->Rdi);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R8 =%016I64x R9 =%016I64x R10=%016I64x\r\n", Context->R8, Context->R9, Context->R10);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R11=%016I64x R12=%016I64x R13=%016I64x\r\n", Context->R11, Context->R12, Context->R13);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R14=%016I64x R15=%016I64x\r\n", Context->R14, Context->R15);
        KiDisplayAndLogBugCheckString(Line);
    }
}

/* Intel SDM: CPUID.07H:EDX[15] marks a hybrid part, CPUID.1AH:EAX[31:24] is the core type of the processor running it */
#define KI_CPUID_HYBRID                 (1 << 15)
#define KI_CPUID_HYBRID_LEAF            0x1A
#define KI_HYBRID_CORE_TYPE_SHIFT       24
#define KI_HYBRID_CORE_TYPE_CORE        0x40

static PUCHAR KiEfficiencyClassCapture;

static
ULONG_PTR
NTAPI
KiCaptureEfficiencyClass(
    _In_ ULONG_PTR Argument)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    int Registers[4];

    UNREFERENCED_PARAMETER(Argument);
    ASSERT(Prcb->Number < KE_MAXIMUM_PROCESSORS);

    __cpuid(Registers, 0);
    if ((ULONG)Registers[0] < KI_CPUID_HYBRID_LEAF)
        return 0;

    __cpuidex(Registers, 7, 0);
    if ((Registers[3] & KI_CPUID_HYBRID) == 0)
        return 0;

    __cpuidex(Registers, KI_CPUID_HYBRID_LEAF, 0);
    if (((ULONG)Registers[0] >> KI_HYBRID_CORE_TYPE_SHIFT) == KI_HYBRID_CORE_TYPE_CORE)
        KiEfficiencyClassCapture[Prcb->Number] = 1;

    return 0;
}

/* A higher efficiency class is the more performant one, as Windows documents for SYSTEM_CPU_SET_INFORMATION */
CODE_SEG("INIT")
VOID
NTAPI
KiQueryProcessorEfficiencyClasses(
    _Out_writes_all_(KE_MAXIMUM_PROCESSORS) PUCHAR EfficiencyClasses)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    RtlZeroMemory(EfficiencyClasses, KE_MAXIMUM_PROCESSORS);
    KiEfficiencyClassCapture = EfficiencyClasses;
    KeIpiGenericCall(KiCaptureEfficiencyClass, 0);
    KiEfficiencyClassCapture = NULL;

    /* TODO: AMD processors with compact cores are not told apart */
}
