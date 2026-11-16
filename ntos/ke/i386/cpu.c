/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 processor identification, features, cache geometry, control state and power hooks
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <xmmintrin.h>

#define NDEBUG
#include <debug.h>

KTSS KiDoubleFaultTSS;

KTSS KiNMITSS;

ULONG KeI386CpuType;
ULONG KeI386CpuStep;
ULONG KiFastSystemCallDisable = 0;
ULONG KeI386NpxPresent = TRUE;
ULONG KiMXCsrMask = 0;
ULONG MxcsrFeatureMask = 0;
ULONG KeI386XMMIPresent = 0;
ULONG KeI386FxsrPresent = 0;
ULONG KeI386MachineType;
ULONG Ke386Pae = FALSE;
ULONG Ke386NoExecute = FALSE;
ULONG KeLargestCacheLine = 0x40;
ULONG KeDcacheFlushCount = 0;
ULONG KeIcacheFlushCount = 0;
ULONG KiDmaIoCoherency = 0;
ULONG KePrefetchNTAGranularity = 32;
BOOLEAN KiI386PentiumLockErrataPresent;
BOOLEAN KiSMTProcessorsPresent;

UCHAR KiSystemCallExitAdjust;

UCHAR KiSystemCallExitAdjusted;

BOOLEAN KiFastCallCopyDoneOnce;

volatile LONG KiTbFlushTimeStamp;

static const CHAR CmpIntelID[]       = "GenuineIntel";
static const CHAR CmpAmdID[]         = "AuthenticAMD";
static const CHAR CmpCyrixID[]       = "CyrixInstead";
static const CHAR CmpTransmetaID[]   = "GenuineTMx86";
static const CHAR CmpCentaurID[]     = "CentaurHauls";
static const CHAR CmpRiseID[]        = "RiseRiseRise";

typedef union _CPU_SIGNATURE
{
    struct
    {
        ULONG Step : 4;
        ULONG Model : 4;
        ULONG Family : 4;
        ULONG Unused : 4;
        ULONG ExtendedModel : 4;
        ULONG ExtendedFamily : 8;
        ULONG Unused2 : 4;
    };
    ULONG AsULONG;
} CPU_SIGNATURE;

#define FXSAVE_ALIGN 15

#define CX86_CCR1 0xc1

static __inline
UCHAR
getCx86(
    UCHAR reg)
{
    WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)0x22, reg);
    return READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)0x23);
}

static __inline
void
setCx86(
    UCHAR reg,
    UCHAR data)
{
    WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)0x22, reg);
    WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)0x23, data);
}

CODE_SEG("INIT")
ULONG
NTAPI
KiGetCpuVendor(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    CPU_INFO CpuInfo;

    KiCpuId(&CpuInfo, 0);

    *(ULONG*)&Prcb->VendorString[0] = CpuInfo.Ebx;
    *(ULONG*)&Prcb->VendorString[4] = CpuInfo.Edx;
    *(ULONG*)&Prcb->VendorString[8] = CpuInfo.Ecx;
    Prcb->VendorString[12] = 0;

    if (!strcmp(Prcb->VendorString, CmpIntelID) ||
        !strcmp(Prcb->VendorString, "GenuineIotel"))
    {
        return CPU_INTEL;
    }
    else if (!strcmp(Prcb->VendorString, CmpAmdID) ||
             !strcmp(Prcb->VendorString, "HygonGenuine"))
    {
        return CPU_AMD;
    }
    else if (!strcmp(Prcb->VendorString, CmpCyrixID))
    {
        DPRINT1("Cyrix CPU support not fully tested!\n");
        return CPU_CYRIX;
    }
    else if (!strcmp(Prcb->VendorString, CmpTransmetaID))
    {
        DPRINT1("Transmeta CPU support not fully tested!\n");
        return CPU_TRANSMETA;
    }
    else if (!strcmp(Prcb->VendorString, CmpCentaurID) ||
             !strcmp(Prcb->VendorString, "  Shanghai  "))
    {
        DPRINT1("Centaur CPU support not fully tested!\n");
        return CPU_CENTAUR;
    }
    else if (!strcmp(Prcb->VendorString, CmpRiseID))
    {
        DPRINT1("Rise CPU support not fully tested!\n");
        return CPU_RISE;
    }

    DPRINT1("%s CPU support not fully tested!\n", Prcb->VendorString);
    return CPU_UNKNOWN;
}

CODE_SEG("INIT")
VOID
NTAPI
KiSetProcessorType(VOID)
{
    CPU_INFO CpuInfo;
    CPU_SIGNATURE CpuSignature;
    BOOLEAN ExtendModel;
    ULONG Stepping, Type;

    KiCpuId(&CpuInfo, 1);

    CpuSignature.AsULONG = CpuInfo.Eax;
    Stepping = CpuSignature.Model;
    ExtendModel = (CpuSignature.Family == 15);
    if (CpuSignature.Family == 6)
    {
        ULONG Vendor = KiGetCpuVendor();
        ExtendModel |= (Vendor == CPU_INTEL);
        ExtendModel |= (Vendor == CPU_CENTAUR);
    }
    if (ExtendModel)
    {
        Stepping |= (CpuSignature.ExtendedModel << 4);
    }
    Stepping = (Stepping << 8) | CpuSignature.Step;
    Type = CpuSignature.Family;
    if (CpuSignature.Family == 15)
    {
        Type += min(CpuSignature.ExtendedFamily, 0xF0);
    }

    KeGetCurrentPrcb()->CpuID = TRUE;
    KeGetCurrentPrcb()->CpuType = (UCHAR)Type;
    KeGetCurrentPrcb()->CpuStep = (USHORT)Stepping;
}

CODE_SEG("INIT")
ULONG64
NTAPI
KiGetFeatureBits(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Vendor;
    ULONG64 FeatureBits = KF_WORKING_PTE;
    CPU_INFO CpuInfo, DummyCpuInfo;
    UCHAR Ccr1;
    BOOLEAN ExtendedCPUID = TRUE;
    ULONG CpuFeatures = 0;

    Vendor = KiGetCpuVendor();

    if (!Vendor) return FeatureBits;

    KiCpuId(&CpuInfo, 1);

    Prcb->InitialApicId = (UCHAR)(CpuInfo.Ebx >> 24);

    switch (Vendor)
    {
        case CPU_INTEL:

            if (Prcb->CpuType == 6)
            {
                __writemsr(0x8B, 0);
                KiCpuId(&DummyCpuInfo, 1);
                Prcb->UpdateSignature.QuadPart = __readmsr(0x8B);
            }
            else if (Prcb->CpuType == 5)
            {
                /* On P5, enable workaround for the LOCK errata. */
                KiI386PentiumLockErrataPresent = TRUE;
            }

            if (((CpuInfo.Eax & 0x0FF0) == 0x0610 && (CpuInfo.Eax & 0x000F) <= 0x9) ||
                ((CpuInfo.Eax & 0x0FF0) == 0x0630 && (CpuInfo.Eax & 0x000F) <= 0x4))
            {
                FeatureBits &= ~KF_WORKING_PTE;
            }

            if ((Prcb->CpuType < 6) ||
                ((Prcb->CpuType == 6) && (Prcb->CpuStep < 0x0303)))
            {
                CpuInfo.Edx &= ~0x800;
            }

            break;

        case CPU_AMD:

            if ((CpuInfo.Eax & 0x0F00) == 0x0500)
            {
                switch (CpuInfo.Eax & 0x00F0)
                {
                    case 0x0010:

                        if ((CpuInfo.Eax & 0x000F) > 0x03) break;

                    case 0x0000:

                        CpuInfo.Edx &= ~0x2000;
                        break;

                    case 0x0080:

                        if ((CpuInfo.Eax & 0x000F) >= 0x8) FeatureBits |= KF_AMDK6MTRR;
                        break;

                    case 0x0090:
                    case 0x00D0:

                        FeatureBits |= KF_AMDK6MTRR;
                        break;
                }
            }
            else if((CpuInfo.Eax & 0x0F00) < 0x0500)
            {
                CpuInfo.Edx &= ~(0x08 | 0x2000 | 0x8000);

                ExtendedCPUID = FALSE;
            }

            break;

        case CPU_CYRIX:

            /* Workaround the "COMA" bug on 6x family of Cyrix CPUs */
            if (Prcb->CpuType == 6 &&
                Prcb->CpuStep <= 1)
            {
                Ccr1 = getCx86(CX86_CCR1);

                Ccr1 |= 0x10;

                setCx86(CX86_CCR1, Ccr1);
            }

            break;

        case CPU_TRANSMETA:

            if ((CpuInfo.Eax & 0x0FFF) >= 0x0542)
            {
                __writemsr(0x80860004, __readmsr(0x80860004) | 0x0100);
                FeatureBits |= KF_CMPXCHG8B;
            }

            break;

        case CPU_CENTAUR:
        case CPU_RISE:

            FeatureBits |= KF_CMPXCHG8B;

            break;
    }

    if (CpuInfo.Ecx & X86_FEATURE_SSE3) FeatureBits |= KF_SSE3;
    if (CpuInfo.Ecx & X86_FEATURE_SSSE3) FeatureBits |= KF_SSSE3;
    if (CpuInfo.Ecx & X86_FEATURE_SSE4_1) FeatureBits |= KF_SSE4_1;
    if (CpuInfo.Ecx & X86_FEATURE_SSE4_2) FeatureBits |= KF_SSE4_2;
    if (CpuInfo.Ecx & X86_FEATURE_XSAVE) FeatureBits |= KF_XSTATE;
    if (CpuInfo.Ecx & X86_FEATURE_RDRAND) FeatureBits |= KF_RDRAND;

    CpuFeatures = CpuInfo.Edx;

    if (CpuFeatures & X86_FEATURE_VME)     FeatureBits |= KF_V86_VIS | KF_CR4;
    if (CpuFeatures & X86_FEATURE_PSE)     FeatureBits |= KF_LARGE_PAGE | KF_CR4;
    if (CpuFeatures & X86_FEATURE_TSC)     FeatureBits |= KF_RDTSC;
    if (CpuFeatures & X86_FEATURE_CX8)     FeatureBits |= KF_CMPXCHG8B;
    if (CpuFeatures & X86_FEATURE_SYSCALL) FeatureBits |= KF_FAST_SYSCALL;
    if (CpuFeatures & X86_FEATURE_MTRR)    FeatureBits |= KF_MTRR;
    if (CpuFeatures & X86_FEATURE_PGE)     FeatureBits |= KF_GLOBAL_PAGE | KF_CR4;
    if (CpuFeatures & X86_FEATURE_CMOV)    FeatureBits |= KF_CMOV;
    if (CpuFeatures & X86_FEATURE_PAT)     FeatureBits |= KF_PAT;
    if (CpuFeatures & X86_FEATURE_DS)      FeatureBits |= KF_DTS;
    if (CpuFeatures & X86_FEATURE_MMX)     FeatureBits |= KF_MMX;
    if (CpuFeatures & X86_FEATURE_FXSR)    FeatureBits |= KF_FXSR;
    if (CpuFeatures & X86_FEATURE_SSE)     FeatureBits |= KF_XMMI;
    if (CpuFeatures & X86_FEATURE_SSE2)    FeatureBits |= KF_XMMI64;

    if (CpuFeatures & X86_FEATURE_HT)
    {
        Prcb->LogicalProcessorsPerPhysicalProcessor = (UCHAR)(CpuInfo.Ebx >> 16);
        if (Prcb->LogicalProcessorsPerPhysicalProcessor > 1)
        {
            KiSMTProcessorsPresent = TRUE;
        }
    }
    else
    {
        Prcb->LogicalProcessorsPerPhysicalProcessor = 1;
    }

    if (ExtendedCPUID)
    {
        KiCpuId(&CpuInfo, 0x80000000);
        if ((CpuInfo.Eax & 0xffffff00) == 0x80000000)
        {
            if (CpuInfo.Eax >= 0x80000001)
            {
                KiCpuId(&CpuInfo, 0x80000001);

                if (CpuInfo.Edx & X86_FEATURE_NX) FeatureBits |= KF_NX_BIT;

                switch (Vendor)
                {
                    case CPU_AMD:
                    case CPU_CENTAUR:
                        if (CpuInfo.Edx & 0x80000000) FeatureBits |= KF_3DNOW;
                        break;
                }
            }
        }
    }

    return FeatureBits;
}

#if DBG
CODE_SEG("INIT")
VOID
KiReportCpuFeatures(VOID)
{
    ULONG CpuFeatures = 0;
    CPU_INFO CpuInfo;

    if (KiGetCpuVendor())
    {
        KiCpuId(&CpuInfo, 1);
        CpuFeatures = CpuInfo.Edx;
    }

    DPRINT1("Supported CPU features:");

#define print_kf_bit(kf_value) if (KeFeatureBits & kf_value) DbgPrint(" " #kf_value)
    print_kf_bit(KF_V86_VIS);
    print_kf_bit(KF_RDTSC);
    print_kf_bit(KF_CR4);
    print_kf_bit(KF_CMOV);
    print_kf_bit(KF_GLOBAL_PAGE);
    print_kf_bit(KF_LARGE_PAGE);
    print_kf_bit(KF_MTRR);
    print_kf_bit(KF_CMPXCHG8B);
    print_kf_bit(KF_MMX);
    print_kf_bit(KF_WORKING_PTE);
    print_kf_bit(KF_PAT);
    print_kf_bit(KF_FXSR);
    print_kf_bit(KF_FAST_SYSCALL);
    print_kf_bit(KF_XMMI);
    print_kf_bit(KF_3DNOW);
    print_kf_bit(KF_AMDK6MTRR);
    print_kf_bit(KF_XMMI64);
    print_kf_bit(KF_DTS);
    print_kf_bit(KF_NX_BIT);
    print_kf_bit(KF_NX_DISABLED);
    print_kf_bit(KF_NX_ENABLED);
#undef print_kf_bit

#define print_cf(cpu_flag) if (CpuFeatures & cpu_flag) DbgPrint(" " #cpu_flag)
    print_cf(X86_FEATURE_PAE);
    print_cf(X86_FEATURE_APIC);
    print_cf(X86_FEATURE_HT);
#undef print_cf

    DbgPrint("\n");
}
#endif

CODE_SEG("INIT")
VOID
NTAPI
KiGetCacheInformation(VOID)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    CPU_INFO CpuInfo;
    ULONG CacheRequests = 0, i;
    ULONG CurrentRegister;
    UCHAR RegisterByte, Associativity = 0;
    ULONG Size, CacheLine = 64, CurrentSize = 0;
    BOOLEAN FirstPass = TRUE;

    Pcr->SecondLevelCacheSize = 0;

    switch (KiGetCpuVendor())
    {
        case CPU_INTEL:

            KiCpuId(&CpuInfo, 0);
            if (CpuInfo.Eax >= 2)
            {
                do
                {
                    KiCpuId(&CpuInfo, 2);

                    if (FirstPass)
                    {
                        CacheRequests = CpuInfo.Eax & 0xFF;
                        CpuInfo.Eax &= 0xFFFFFF00;

                        FirstPass = FALSE;
                    }

                    for (i = 0; i < 4; i++)
                    {
                        CurrentRegister = CpuInfo.AsUINT32[i];

                        if (CurrentRegister & 0x80000000) continue;

                        while (CurrentRegister)
                        {
                            RegisterByte = (UCHAR)(CurrentRegister & 0xFF);
                            CurrentRegister >>= 8;
                            if (!RegisterByte) continue;

                            Size = 0;
                            switch (RegisterByte)
                            {
                                case 0x06:
                                case 0x08:
                                    KePrefetchNTAGranularity = 32;
                                    break;
                                case 0x09:
                                    KePrefetchNTAGranularity = 64;
                                    break;
                                case 0x0a:
                                case 0x0c:
                                    KePrefetchNTAGranularity = 32;
                                    break;
                                case 0x0d:
                                case 0x0e:
                                    KePrefetchNTAGranularity = 64;
                                    break;
                                case 0x1d:
                                    Size = 128 * 1024;
                                    Associativity = 2;
                                    break;
                                case 0x21:
                                    Size = 256 * 1024;
                                    Associativity = 8;
                                    break;
                                case 0x24:
                                    Size = 1024 * 1024;
                                    Associativity = 16;
                                    break;
                                case 0x2c:
                                case 0x30:
                                    KePrefetchNTAGranularity = 64;
                                    break;
                                case 0x41:
                                case 0x42:
                                case 0x43:
                                case 0x44:
                                case 0x45:
                                    Size = (1 << (RegisterByte - 0x41)) * 128 * 1024;
                                    Associativity = 4;
                                    break;
                                case 0x48:
                                    Size = 3 * 1024 * 1024;
                                    Associativity = 12;
                                    break;
                                case 0x49:
                                    Size = 4 * 1024 * 1024;
                                    Associativity = 16;
                                    break;
                                case 0x4e:
                                    Size = 6 * 1024 * 1024;
                                    Associativity = 24;
                                    break;
                                case 0x60:
                                case 0x66:
                                case 0x67:
                                case 0x68:
                                    KePrefetchNTAGranularity = 64;
                                    break;
                                case 0x78:
                                    Size = 1024 * 1024;
                                    Associativity = 4;
                                    break;
                                case 0x79:
                                case 0x7a:
                                case 0x7b:
                                case 0x7c:
                                case 0x7d:
                                    Size = (1 << (RegisterByte - 0x79)) * 128 * 1024;
                                    Associativity = 8;
                                    break;
                                case 0x7f:
                                    Size = 512 * 1024;
                                    Associativity = 2;
                                    break;
                                case 0x80:
                                    Size = 512 * 1024;
                                    Associativity = 8;
                                    break;
                                case 0x82:
                                case 0x83:
                                case 0x84:
                                case 0x85:
                                    Size = (1 << (RegisterByte - 0x82)) * 256 * 1024;
                                    Associativity = 8;
                                    break;
                                case 0x86:
                                    Size = 512 * 1024;
                                    Associativity = 4;
                                    break;
                                case 0x87:
                                    Size = 1024 * 1024;
                                    Associativity = 8;
                                    break;
                                case 0xf0:
                                    KePrefetchNTAGranularity = 64;
                                    break;
                                case 0xf1:
                                    KePrefetchNTAGranularity = 128;
                                    break;
                            }
                            if (Size && (Size / Associativity) > CurrentSize)
                            {
                                CurrentSize = Size / Associativity;
                                Pcr->SecondLevelCacheSize = Size;
                                Pcr->SecondLevelCacheAssociativity = Associativity;
                            }
                        }
                    }
                } while (--CacheRequests);
            }
            break;

        case CPU_AMD:

            KiCpuId(&CpuInfo, 0x80000000);
            if (CpuInfo.Eax >= 0x80000005)
            {
                KiCpuId(&CpuInfo, 0x80000005);
                KePrefetchNTAGranularity = CpuInfo.Ecx & 0xFF;

                KiCpuId(&CpuInfo, 0x80000000);
                if (CpuInfo.Eax >= 0x80000006)
                {
                    KiCpuId(&CpuInfo, 0x80000006);

                    CacheLine = CpuInfo.Ecx & 0xFF;

                    RegisterByte = (CpuInfo.Ecx >> 12) & 0xFF;
                    switch (RegisterByte)
                    {
                        case 2:
                            Associativity = 2;
                            break;

                        case 4:
                            Associativity = 4;
                            break;

                        case 6:
                            Associativity = 8;
                            break;

                        case 8:
                        case 15:
                            Associativity = 16;
                            break;

                        default:
                            Associativity = 1;
                            break;
                    }

                    Size = (CpuInfo.Ecx >> 16) << 10;

                    /* Hack for Model 6, Steping 300 */
                    if ((KeGetCurrentPrcb()->CpuType == 6) &&
                        (KeGetCurrentPrcb()->CpuStep == 0x300))
                    {
                        Size = 64 * 1024;
                    }

                    Pcr->SecondLevelCacheSize = Size;
                    Pcr->SecondLevelCacheAssociativity = Associativity;
                }
            }
            break;

        case CPU_CYRIX:
        case CPU_TRANSMETA:
        case CPU_CENTAUR:
        case CPU_RISE:

            /* FIXME */
            break;
    }

    if (CacheLine > KeLargestCacheLine) KeLargestCacheLine = CacheLine;
    DPRINT1("Prefetch Cache: %lu bytes\tL2 Cache: %lu bytes\tL2 Cache Line: %lu bytes\tL2 Cache Associativity: %lu\n",
            KePrefetchNTAGranularity,
            Pcr->SecondLevelCacheSize,
            KeLargestCacheLine,
            Pcr->SecondLevelCacheAssociativity);
}

CODE_SEG("INIT")
VOID
NTAPI
KiSetCR0Bits(VOID)
{
    ULONG Cr0;

    Cr0 = __readcr0();

    if (KeGetCurrentPrcb()->CpuType > 3) Cr0 |= CR0_WP;

    __writecr0(Cr0);
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeTSS2(IN PKTSS Tss,
                 _In_opt_ PKGDTENTRY TssEntry)
{
    PUCHAR p;

    if (TssEntry)
    {
        TssEntry->LimitLow = sizeof(KTSS) - 1;
        TssEntry->HighWord.Bits.LimitHi = 0;
    }

    ASSERT(IOPM_COUNT == 1);
    RtlFillMemory(Tss->IoMaps[0].IoMap, IOPM_FULL_SIZE, 0xFF);

    p = (PUCHAR)(Tss->IoMaps[0].DirectionMap);
    RtlZeroMemory(p, IOPM_DIRECTION_MAP_SIZE);

    p[0] = 4;
    p[3] = 0x18;
    p[4] = 0x18;

    p = Tss->IntDirectionMap;
    RtlZeroMemory(Tss->IntDirectionMap, IOPM_DIRECTION_MAP_SIZE);

    p[0] = 4;
    p[3] = 0x18;
    p[4] = 0x18;
}

VOID
NTAPI
KiInitializeTSS(
    _In_ PKTSS Tss)
{
    Tss->IoMapBase = KiComputeIopmOffset(IO_ACCESS_MAP_NONE);

    Tss->Flags = 0;

    Tss->LDT = 0;
    Tss->Ss0 = KGDT_R0_DATA;
}

CODE_SEG("INIT")
VOID
FASTCALL
Ki386InitializeTss(IN PKTSS Tss,
                   _In_ PKIDTENTRY Idt,
                   _In_ PKGDTENTRY Gdt)
{
    PKGDTENTRY TssEntry, TaskGateEntry;

    TssEntry = &Gdt[KGDT_TSS / sizeof(KGDTENTRY)];
    TssEntry->HighWord.Bits.Type = I386_TSS;
    TssEntry->HighWord.Bits.Pres = 1;
    TssEntry->HighWord.Bits.Dpl = 0;
    KiInitializeTSS2(Tss, TssEntry);
    KiInitializeTSS(Tss);

    Ke386SetTr(KGDT_TSS);

    TaskGateEntry = (PKGDTENTRY)&Idt[8];
    TaskGateEntry->HighWord.Bits.Type = I386_TASK_GATE;
    TaskGateEntry->HighWord.Bits.Pres = 1;
    TaskGateEntry->HighWord.Bits.Dpl = 0;
    ((PKIDTENTRY)TaskGateEntry)->Selector = KGDT_DF_TSS;

    Tss = &KiDoubleFaultTSS;
    KiInitializeTSS(Tss);
    Tss->CR3 = __readcr3();
    Tss->Esp0 = KiDoubleFaultStack;
    Tss->Esp = KiDoubleFaultStack;
    Tss->Eip = PtrToUlong(KiTrap08);
    Tss->Cs = KGDT_R0_CODE;
    Tss->Fs = KGDT_R0_PCR;
    Tss->Ss = Ke386GetSs();
    Tss->Es = KGDT_R3_DATA | RPL_MASK;
    Tss->Ds = KGDT_R3_DATA | RPL_MASK;

    TssEntry = &Gdt[KGDT_DF_TSS / sizeof(KGDTENTRY)];
    TssEntry->HighWord.Bits.Type = I386_TSS;
    TssEntry->HighWord.Bits.Pres = 1;
    TssEntry->HighWord.Bits.Dpl = 0;
    TssEntry->BaseLow = (USHORT)((ULONG_PTR)Tss & 0xFFFF);
    TssEntry->HighWord.Bytes.BaseMid = (UCHAR)((ULONG_PTR)Tss >> 16);
    TssEntry->HighWord.Bytes.BaseHi = (UCHAR)((ULONG_PTR)Tss >> 24);
    TssEntry->LimitLow = KTSS_IO_MAPS;

    TaskGateEntry = (PKGDTENTRY)&Idt[2];
    TaskGateEntry->HighWord.Bits.Type = I386_TASK_GATE;
    TaskGateEntry->HighWord.Bits.Pres = 1;
    TaskGateEntry->HighWord.Bits.Dpl = 0;
    ((PKIDTENTRY)TaskGateEntry)->Selector = KGDT_NMI_TSS;

    Tss = &KiNMITSS;
    KiInitializeTSS(Tss);
    Tss->CR3 = __readcr3();
    Tss->Esp0 = KiDoubleFaultStack;
    Tss->Esp = KiDoubleFaultStack;
    Tss->Eip = PtrToUlong(KiTrap02);
    Tss->Cs = KGDT_R0_CODE;
    Tss->Fs = KGDT_R0_PCR;
    Tss->Ss = Ke386GetSs();
    Tss->Es = KGDT_R3_DATA | RPL_MASK;
    Tss->Ds = KGDT_R3_DATA | RPL_MASK;

    TssEntry = &Gdt[KGDT_NMI_TSS / sizeof(KGDTENTRY)];
    TssEntry->HighWord.Bits.Type = I386_TSS;
    TssEntry->HighWord.Bits.Pres = 1;
    TssEntry->HighWord.Bits.Dpl = 0;
    TssEntry->BaseLow = (USHORT)((ULONG_PTR)Tss & 0xFFFF);
    TssEntry->HighWord.Bytes.BaseMid = (UCHAR)((ULONG_PTR)Tss >> 16);
    TssEntry->HighWord.Bytes.BaseHi = (UCHAR)((ULONG_PTR)Tss >> 24);
    TssEntry->LimitLow = KTSS_IO_MAPS;
}

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
#if !defined(_GLOBAL_PAGES_ARE_AWESOME_)

    __writecr3(__readcr3());

#else

    if (KeFeatureBits & KF_GLOBAL_PAGE)
    {
        ULONG Cr4;

        /* Disable PGE (Note: may not have been enabled yet) */
        Cr4 = __readcr4();
        __writecr4(Cr4 & ~CR4_PGE);

        __writecr3(__readcr3());

        __writecr4(Cr4);
    }
    else
    {
        __writecr3(__readcr3());
    }

#endif
}

VOID
NTAPI
KiRestoreProcessorControlState(
    PKPROCESSOR_STATE ProcessorState)
{
    PKGDTENTRY TssEntry;

    __writecr0(ProcessorState->SpecialRegisters.Cr0);
    Ke386SetCr2(ProcessorState->SpecialRegisters.Cr2);
    __writecr3(ProcessorState->SpecialRegisters.Cr3);
    if (KeFeatureBits & KF_CR4) __writecr4(ProcessorState->SpecialRegisters.Cr4);

    __writedr(0, ProcessorState->SpecialRegisters.KernelDr0);
    __writedr(1, ProcessorState->SpecialRegisters.KernelDr1);
    __writedr(2, ProcessorState->SpecialRegisters.KernelDr2);
    __writedr(3, ProcessorState->SpecialRegisters.KernelDr3);
    __writedr(6, ProcessorState->SpecialRegisters.KernelDr6);
    __writedr(7, ProcessorState->SpecialRegisters.KernelDr7);

    Ke386SetGlobalDescriptorTable(&ProcessorState->SpecialRegisters.Gdtr.Limit);
    __lidt(&ProcessorState->SpecialRegisters.Idtr.Limit);

    TssEntry = (PKGDTENTRY)(ProcessorState->SpecialRegisters.Gdtr.Base +
                            ProcessorState->SpecialRegisters.Tr);
    TssEntry->HighWord.Bytes.Flags1 &= ~0x2;

    Ke386SetTr(ProcessorState->SpecialRegisters.Tr);
    Ke386SetLocalDescriptorTable(ProcessorState->SpecialRegisters.Ldtr);
}

VOID
NTAPI
KiSaveProcessorControlState(
    _Out_ PKPROCESSOR_STATE ProcessorState)
{
    ProcessorState->SpecialRegisters.Cr0 = __readcr0();
    ProcessorState->SpecialRegisters.Cr2 = __readcr2();
    ProcessorState->SpecialRegisters.Cr3 = __readcr3();
    ProcessorState->SpecialRegisters.Cr4 = (KeFeatureBits & KF_CR4) ?
                                           __readcr4() : 0;

    ProcessorState->SpecialRegisters.KernelDr0 = __readdr(0);
    ProcessorState->SpecialRegisters.KernelDr1 = __readdr(1);
    ProcessorState->SpecialRegisters.KernelDr2 = __readdr(2);
    ProcessorState->SpecialRegisters.KernelDr3 = __readdr(3);
    ProcessorState->SpecialRegisters.KernelDr6 = __readdr(6);
    ProcessorState->SpecialRegisters.KernelDr7 = __readdr(7);
    __writedr(7, 0);

    Ke386GetGlobalDescriptorTable(&ProcessorState->SpecialRegisters.Gdtr.Limit);
    __sidt(&ProcessorState->SpecialRegisters.Idtr.Limit);
    ProcessorState->SpecialRegisters.Tr = Ke386GetTr();
    Ke386GetLocalDescriptorTable(&ProcessorState->SpecialRegisters.Ldtr);
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeMachineType(VOID)
{
    KeI386MachineType = KeLoaderBlock->u.I386.MachineType & 0x000FF;
}

CODE_SEG("INIT")
ULONG_PTR
NTAPI
KiLoadFastSyscallMachineSpecificRegisters(IN ULONG_PTR Context)
{
    __writemsr(0x174, KGDT_R0_CODE);
    __writemsr(0x175, (ULONG_PTR)KeGetCurrentPrcb()->DpcStack);

    __writemsr(0x176, (ULONG_PTR)KiFastCallEntry);
    return 0;
}

CODE_SEG("INIT")
VOID
NTAPI
KiRestoreFastSyscallReturnState(VOID)
{
    if (KeFeatureBits & KF_FAST_SYSCALL)
    {
        if (KiFastSystemCallDisable)
        {
            KeFeatureBits &= ~KF_FAST_SYSCALL;
            KiFastCallExitHandler = KiSystemCallTrapReturn;
            DPRINT1("Support for SYSENTER disabled.\n");
        }
        else
        {
            KeIpiGenericCall(KiLoadFastSyscallMachineSpecificRegisters, 0);

            KiFastCallExitHandler = KiSystemCallSysExitReturn;
            DPRINT("Support for SYSENTER detected.\n");
        }
    }
    else
    {
        KiFastCallExitHandler = KiSystemCallTrapReturn;
        DPRINT1("No support for SYSENTER detected.\n");
    }
}

CODE_SEG("INIT")
ULONG_PTR
NTAPI
Ki386EnableDE(IN ULONG_PTR Context)
{
    __writecr4(__readcr4() | CR4_DE);
    return 0;
}

CODE_SEG("INIT")
ULONG_PTR
NTAPI
Ki386EnableFxsr(IN ULONG_PTR Context)
{
    __writecr4(__readcr4() | CR4_FXSR);
    return 0;
}

CODE_SEG("INIT")
ULONG_PTR
NTAPI
Ki386EnableXMMIExceptions(IN ULONG_PTR Context)
{
    PKIDTENTRY IdtEntry;

    IdtEntry = &((PKIPCR)KeGetPcr())->IDT[0x13];

    IdtEntry->Selector = KGDT_R0_CODE;
    IdtEntry->Offset = ((ULONG_PTR)KiTrap13 & 0xFFFF);
    IdtEntry->ExtendedOffset = ((ULONG_PTR)KiTrap13 >> 16) & 0xFFFF;
    ((PKIDT_ACCESS)&IdtEntry->Access)->Dpl = 0;
    ((PKIDT_ACCESS)&IdtEntry->Access)->Present = 1;
    ((PKIDT_ACCESS)&IdtEntry->Access)->SegmentType = I386_INTERRUPT_GATE;

    __writecr4(__readcr4() | CR4_XMMEXCPT);
    return 0;
}

CODE_SEG("INIT")
VOID
NTAPI
KiI386PentiumLockErrataFixup(VOID)
{
    KDESCRIPTOR IdtDescriptor = {0, 0, 0};
    PKIDTENTRY NewIdt, NewIdt2;
    PMMPTE PointerPte;

    NewIdt = ExAllocatePool(NonPagedPool, 2 * PAGE_SIZE);

    NewIdt2 = (PVOID)((ULONG_PTR)NewIdt + PAGE_SIZE - (7 * sizeof(KIDTENTRY)));

    _disable();

    __sidt(&IdtDescriptor.Limit);
    RtlCopyMemory(NewIdt2,
                  (PVOID)IdtDescriptor.Base,
                  IdtDescriptor.Limit + 1);
    IdtDescriptor.Base = (ULONG)NewIdt2;

    __lidt(&IdtDescriptor.Limit);
    ((PKIPCR)KeGetPcr())->IDT = NewIdt2;

    _enable();

    PointerPte = MiAddressToPte(NewIdt);
    ASSERT(PointerPte->u.Hard.Write == 1);
    PointerPte->u.Hard.Write = 0;
    KeInvalidateTlbEntry(NewIdt);
}

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    if (KeI386CpuType < 6) return FALSE;

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

    KiCpuId(&CpuInfo, 1);
    if (!(CpuInfo.Edx & (1UL << 19)))
    {
        __wbinvd();
        return;
    }

    CacheLineSize = KeLargestCacheLine ? KeLargestCacheLine : 64;
    Address = (ULONG_PTR)BaseAddress & ~((ULONG_PTR)CacheLineSize - 1);
    EndAddress = (ULONG_PTR)BaseAddress + Length;

    /* Keep the baseline kernel usable without SSE2. CLFLUSH is checked above. */
    KeMemoryBarrier();
    do
    {
#if defined(__GNUC__)
        __asm__ __volatile__("clflush (%0)" :: "r" (Address) : "memory");
#else
        _mm_clflush((PVOID)Address);
#endif
        Address += CacheLineSize;
    } while ((Address < EndAddress) && (Address != 0));
    KeMemoryBarrier();
}

VOID
NTAPI
KiSaveProcessorState(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    Prcb->ProcessorState.ContextFrame.ContextFlags = CONTEXT_FULL |
                                                     CONTEXT_DEBUG_REGISTERS;
    KeTrapFrameToContext(TrapFrame, NULL, &Prcb->ProcessorState.ContextFrame);

    KiSaveProcessorControlState(&Prcb->ProcessorState);
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
KiIsNpxErrataPresent(VOID)
{
    static double Value1 = 4195835.0, Value2 = 3145727.0;
    INT ErrataPresent;
    ULONG Cr0;

    ASSERT(!(__readeflags() & EFLAGS_INTERRUPT_MASK));

    Cr0 = __readcr0();
    __writecr0(Cr0 & ~(CR0_MP | CR0_TS | CR0_EM));

    Ke386FnInit();

#ifdef __GNUC__
    __asm__ __volatile__
    (
        "fldl %1\n\t"
        "fdivl %2\n\t"
        "fmull %2\n\t"
        "fldl %1\n\t"
        "fsubp\n\t"
        "fistpl %0\n\t"
        : "=m" (ErrataPresent)
        : "m" (Value1),
          "m" (Value2)
    );
#else
    __asm
    {
        fld Value1
        fdiv Value2
        fmul Value2
        fld Value1
        fsubp st(1), st(0)
        fistp ErrataPresent
    };
#endif

    __writecr0(Cr0);

    /* Return if there's an errata */
    return ErrataPresent != 0;
}

VOID
NTAPI
KiFlushNPXState(
    _In_ PFLOATING_SAVE_AREA SaveArea)
{
    ULONG EFlags, Cr0;
    PKTHREAD Thread, NpxThread;
    PFX_SAVE_AREA FxSaveArea;

    EFlags = __readeflags();
    _disable();

    Thread = KeGetCurrentThread();

    if (Thread->NpxState != NPX_STATE_LOADED)
    {
        if (!SaveArea)
        {
            __writeeflags(EFlags);
            return;
        }

        ASSERT(KeI386FxsrPresent == TRUE);

        Cr0 = __readcr0();
        if (Cr0 & (CR0_MP | CR0_TS | CR0_EM))
        {
            __writecr0(Cr0 & ~(CR0_MP | CR0_TS | CR0_EM));
        }

        NpxThread = KeGetCurrentPrcb()->NpxThread;
        if ((NpxThread) && (NpxThread->NpxState == NPX_STATE_LOADED))
        {
            FxSaveArea = KiGetThreadNpxArea(NpxThread);
            Ke386FxSave(FxSaveArea);

            NpxThread->NpxState = NPX_STATE_NOT_LOADED;
        }

        FxSaveArea = KiGetThreadNpxArea(Thread);
        Ke386FxStore(FxSaveArea);
    }
    else
    {
        Cr0 = __readcr0();
        if (Cr0 & (CR0_MP | CR0_TS | CR0_EM))
        {
            __writecr0(Cr0 & ~(CR0_MP | CR0_TS | CR0_EM));
        }

        FxSaveArea = KiGetThreadNpxArea(Thread);
        Thread->NpxState = NPX_STATE_NOT_LOADED;

        if (KeI386FxsrPresent) Ke386FxSave(FxSaveArea);
    }

    if (SaveArea) Ke386FnSave(SaveArea);

    KeGetCurrentPrcb()->NpxThread = NULL;

    Cr0 |= NPX_STATE_NOT_LOADED;
    Cr0 |= FxSaveArea->Cr0NpxState;
    __writecr0(Cr0);

    __writeeflags(EFlags);
}

VOID
NTAPI
KiCoprocessorError(VOID)
{
    PFX_SAVE_AREA NpxArea;

    NpxArea = KiGetThreadNpxArea(KeGetCurrentThread());

    NpxArea->Cr0NpxState = CR0_TS;
    __writecr0(__readcr0() | CR0_TS);
}

#if defined(__clang__)
__attribute__((__target__("sse")))
#endif
NTSTATUS
NTAPI
KeSaveFloatingPointState(
    _Out_ PKFLOATING_SAVE Save)
{
    PFLOATING_SAVE_CONTEXT FsContext;
    PFX_SAVE_AREA FxSaveAreaFrame;
    PKPRCB CurrentPrcb;

    ASSERT(Save);
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ASSERT(KeI386NpxPresent);

    FsContext = ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(FLOATING_SAVE_CONTEXT),
                                      TAG_FLOATING_POINT_CONTEXT);
    if (!FsContext)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    FsContext->Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                              sizeof(FX_SAVE_AREA) + FXSAVE_ALIGN,
                                              TAG_FLOATING_POINT_FX);
    if (!FsContext->Buffer)
    {
        ExFreePoolWithTag(FsContext, TAG_FLOATING_POINT_CONTEXT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Now cache the allocated buffer into the save area
     * and align the said area to a 16-byte boundary. Why
     * do we have to do this is because of ExAllocate function.
     * We gave the necessary alignment requirement in the pool
     * allocation size although the function will always return
     * a 8-byte aligned pointer. Aligning the given pointer directly
     * can cause issues when freeing it from memory afterwards. With
     * that said, we have to cache the buffer to the area so that we
     * do not touch or mess the allocated buffer any further.
     */
    FsContext->PfxSaveArea = ALIGN_UP_POINTER_BY(FsContext->Buffer, 16);

    _disable();
    CurrentPrcb = KeGetCurrentPrcb();

    FsContext->CurrentThread = KeGetCurrentThread();

    if (FsContext->CurrentThread != CurrentPrcb->NpxThread)
    {
        if ((CurrentPrcb->NpxThread != NULL) &&
            (CurrentPrcb->NpxThread->NpxState == NPX_STATE_LOADED))
        {
            FxSaveAreaFrame = KiGetThreadNpxArea(CurrentPrcb->NpxThread);

            Ke386SaveFpuState(FxSaveAreaFrame);

            CurrentPrcb->NpxThread->NpxState = NPX_STATE_NOT_LOADED;
            FxSaveAreaFrame->NpxSavedCpu = 0;
        }

        CurrentPrcb->NpxThread = FsContext->CurrentThread;
    }

    Ke386SaveFpuState(FsContext->PfxSaveArea);

    FsContext->OldNpxIrql = KeGetCurrentIrql();

    Ke386FnInit();

    _enable();

    *((PVOID *) Save) = FsContext;
    return STATUS_SUCCESS;
}

#if defined(__clang__)
__attribute__((__target__("sse")))
#endif
NTSTATUS
NTAPI
KeRestoreFloatingPointState(
    _In_ PKFLOATING_SAVE Save)
{
    PFLOATING_SAVE_CONTEXT FsContext;

    ASSERT(Save);
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ASSERT(KeI386NpxPresent);

    FsContext = *((PVOID *) Save);

    if (FsContext->CurrentThread != KeGetCurrentThread())
    {
        KeBugCheckEx(INVALID_FLOATING_POINT_STATE,
                     0x2,
                     (ULONG_PTR)FsContext->CurrentThread,
                     (ULONG_PTR)KeGetCurrentThread(),
                     0);
    }

    if (FsContext->OldNpxIrql != KeGetCurrentIrql())
    {
        KeBugCheckEx(INVALID_FLOATING_POINT_STATE,
                     0x1,
                     (ULONG_PTR)FsContext->OldNpxIrql,
                     (ULONG_PTR)KeGetCurrentIrql(),
                     0);
    }

    _disable();

    Ke386ClearFpExceptions();

    Ke386RestoreFpuState(FsContext->PfxSaveArea);

    _enable();

    ExFreePoolWithTag(FsContext->Buffer, TAG_FLOATING_POINT_FX);
    ExFreePoolWithTag(FsContext, TAG_FLOATING_POINT_CONTEXT);

    return STATUS_SUCCESS;
}

ULONG
NTAPI
KeGetRecommendedSharedDataAlignment(VOID)
{
    return KeLargestCacheLine;
}

VOID
NTAPI
KiFlushTargetEntireTb(
    _In_ PKIPI_CONTEXT PacketContext,
    _In_ PVOID Ignored1,
    _In_ PVOID Ignored2,
    _In_ PVOID Ignored3)
{
    KiIpiSignalPacketDone(PacketContext);

    KeFlushCurrentTb();
}

VOID
NTAPI
KeFlushEntireTb(
    _In_ BOOLEAN Invalid,
    _In_ BOOLEAN AllProcessors)
{
    KIRQL OldIrql;
    KAFFINITY TargetAffinity;
    PKPRCB Prcb = KeGetCurrentPrcb();

    OldIrql = KeRaiseIrqlToSynchLevel();

    /* FIXME: Use KiTbFlushTimeStamp to synchronize TB flush */
    TargetAffinity = KeActiveProcessors.Bitmap[0];
    TargetAffinity &= ~Prcb->SetMember;

    if (TargetAffinity)
    {
        KiIpiSendPacket(TargetAffinity,
                        KiFlushTargetEntireTb,
                        NULL,
                        0,
                        NULL);
    }

    KeFlushCurrentTb();

    InterlockedExchangeAdd(&KiTbFlushTimeStamp, 1);
    KeLowerIrql(OldIrql);
}

VOID
NTAPI
KeSetDmaIoCoherency(
    _In_ ULONG Coherency)
{
    KiDmaIoCoherency = Coherency;
}

CODE_SEG("INIT")
VOID
NTAPI
KiQueryProcessorEfficiencyClasses(
    _Out_writes_all_(MAXIMUM_PROCESSORS) PUCHAR EfficiencyClasses)
{
    /* TODO: report the core types of hybrid processors as efficiency classes */
    RtlZeroMemory(EfficiencyClasses, MAXIMUM_PROCESSORS);
}
