/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the memory and port resource descriptor encoders and decoders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef ULONGLONG (NTAPI *FN_RtlCmDecodeMemIoResource)(PCM_PARTIAL_RESOURCE_DESCRIPTOR, PULONGLONG);
typedef NTSTATUS (NTAPI *FN_RtlCmEncodeMemIoResource)(PCM_PARTIAL_RESOURCE_DESCRIPTOR, UCHAR, ULONGLONG, ULONGLONG);
typedef ULONGLONG (NTAPI *FN_RtlIoDecodeMemIoResource)(PIO_RESOURCE_DESCRIPTOR, PULONGLONG, PULONGLONG, PULONGLONG);
typedef NTSTATUS (NTAPI *FN_RtlIoEncodeMemIoResource)(PIO_RESOURCE_DESCRIPTOR, UCHAR, ULONGLONG, ULONGLONG,
                                                      ULONGLONG, ULONGLONG);

static FN_RtlCmDecodeMemIoResource pRtlCmDecodeMemIoResource;
static FN_RtlCmEncodeMemIoResource pRtlCmEncodeMemIoResource;
static FN_RtlIoDecodeMemIoResource pRtlIoDecodeMemIoResource;
static FN_RtlIoEncodeMemIoResource pRtlIoEncodeMemIoResource;

static
VOID
TestCmResources(VOID)
{
    static const struct
    {
        ULONGLONG Length;
        UCHAR Type;
        USHORT Flags;
        ULONG EncodedLength;
    } Cases[] =
    {
        { 0x1000ULL, CmResourceTypeMemory, 0, 0x1000 },
        { 0xffffffffULL, CmResourceTypeMemory, 0, 0xffffffff },
        { 0x100000000ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_40, 0x1000000 },
        { 0xffffffff00ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_40, 0xffffffff },
        { 0x10000000000ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_48, 0x1000000 },
        { 0xffffffff0000ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_48, 0xffffffff },
        { 0x1000000000000ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_64, 0x10000 },
        { 0xffffffff00000000ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_64, 0xffffffff },
    };
    static const ULONGLONG InvalidLengths[] =
    {
        0x100000001ULL,
        0xffffffff01ULL,
        0x10000000001ULL,
        0x1000000000001ULL,
        MAXULONGLONG,
    };
    CM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor, Before;
    ULONGLONG Start, Length;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Cases); Index++)
    {
        RtlZeroMemory(&Descriptor, sizeof(Descriptor));
        Descriptor.ShareDisposition = CmResourceShareShared;
        Descriptor.Flags = CM_RESOURCE_MEMORY_READ_ONLY;
        Status = pRtlCmEncodeMemIoResource(&Descriptor, CmResourceTypeMemory, Cases[Index].Length, 0x123456789ULL);
        ok(Status == STATUS_SUCCESS, "Case %lu: status 0x%08lx\n", Index, Status);
        ok(Descriptor.Type == Cases[Index].Type, "Case %lu: type %u\n", Index, Descriptor.Type);
        ok(Descriptor.ShareDisposition == CmResourceShareShared, "Case %lu: share %u\n", Index, Descriptor.ShareDisposition);
        ok(Descriptor.Flags == (Cases[Index].Flags | CM_RESOURCE_MEMORY_READ_ONLY), "Case %lu: flags 0x%x\n",
           Index, Descriptor.Flags);
        ok(Descriptor.u.Memory.Length == Cases[Index].EncodedLength, "Case %lu: encoded length 0x%lx\n",
           Index, Descriptor.u.Memory.Length);
        ok(Descriptor.u.Memory.Start.QuadPart == 0x123456789LL, "Case %lu: start 0x%I64x\n",
           Index, Descriptor.u.Memory.Start.QuadPart);

        Start = 0;
        Length = pRtlCmDecodeMemIoResource(&Descriptor, &Start);
        ok(Length == Cases[Index].Length, "Case %lu: decoded length 0x%I64x\n", Index, Length);
        ok(Start == 0x123456789ULL, "Case %lu: decoded start 0x%I64x\n", Index, Start);
        Length = pRtlCmDecodeMemIoResource(&Descriptor, NULL);
        ok(Length == Cases[Index].Length, "Case %lu: decoded length without start 0x%I64x\n", Index, Length);
    }

    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.Type = CmResourceTypeMemoryLarge;
    Descriptor.Flags = CM_RESOURCE_MEMORY_LARGE_48;
    Descriptor.u.Memory48.Start.QuadPart = 0x100000000LL;
    Descriptor.u.Memory48.Length48 = 0x12345678;
    Length = pRtlCmDecodeMemIoResource(&Descriptor, &Start);
    ok(Length == 0x123456780000ULL, "Memory48 decoded length 0x%I64x\n", Length);
    ok(Start == 0x100000000ULL, "Memory48 decoded start 0x%I64x\n", Start);

    Status = pRtlCmEncodeMemIoResource(&Descriptor, CmResourceTypePort, 0x8, 0x3f8);
    ok(Status == STATUS_SUCCESS, "Port encode status 0x%08lx\n", Status);
    ok(Descriptor.Type == CmResourceTypePort, "Port type %u\n", Descriptor.Type);
    ok(Descriptor.u.Port.Length == 0x8, "Port length 0x%lx\n", Descriptor.u.Port.Length);
    ok(Descriptor.u.Port.Start.QuadPart == 0x3f8, "Port start 0x%I64x\n", Descriptor.u.Port.Start.QuadPart);
    Length = pRtlCmDecodeMemIoResource(&Descriptor, &Start);
    ok(Length == 0x8, "Port decoded length 0x%I64x\n", Length);
    ok(Start == 0x3f8, "Port decoded start 0x%I64x\n", Start);

    for (Index = 0; Index < RTL_NUMBER_OF(InvalidLengths); Index++)
    {
        RtlZeroMemory(&Descriptor, sizeof(Descriptor));
        Descriptor.Type = CmResourceTypeMemory;
        Status = pRtlCmEncodeMemIoResource(&Descriptor, CmResourceTypeMemory, InvalidLengths[Index], 0);
        ok(Status == STATUS_UNSUCCESSFUL, "Invalid length 0x%I64x: status 0x%08lx\n", InvalidLengths[Index], Status);
    }

    RtlFillMemory(&Descriptor, sizeof(Descriptor), 0xa5);
    Before = Descriptor;
    Status = pRtlCmEncodeMemIoResource(&Descriptor, CmResourceTypeInterrupt, 1, 0);
    ok(Status == STATUS_INVALID_PARAMETER, "Interrupt type status 0x%08lx\n", Status);
    ok(RtlCompareMemory(&Descriptor, &Before, sizeof(Before)) == sizeof(Before), "Rejected encode changed the descriptor\n");
}

static
VOID
TestIoResources(VOID)
{
    IO_RESOURCE_DESCRIPTOR Descriptor;
    ULONGLONG Alignment, Minimum, Maximum, Length;
    NTSTATUS Status;

    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Status = pRtlIoEncodeMemIoResource(&Descriptor, CmResourceTypeMemory, 0x4000, 0x1000, 0x10000000ULL, 0x1fffffffULL);
    ok(Status == STATUS_SUCCESS, "Memory encode status 0x%08lx\n", Status);
    ok(Descriptor.Type == CmResourceTypeMemory, "Memory type %u\n", Descriptor.Type);
    ok(Descriptor.u.Memory.Length == 0x4000, "Memory length 0x%lx\n", Descriptor.u.Memory.Length);
    ok(Descriptor.u.Memory.Alignment == 0x1000, "Memory alignment 0x%lx\n", Descriptor.u.Memory.Alignment);
    ok(Descriptor.u.Memory.MinimumAddress.QuadPart == 0x10000000LL, "Memory minimum 0x%I64x\n",
       Descriptor.u.Memory.MinimumAddress.QuadPart);
    ok(Descriptor.u.Memory.MaximumAddress.QuadPart == 0x1fffffffLL, "Memory maximum 0x%I64x\n",
       Descriptor.u.Memory.MaximumAddress.QuadPart);
    Alignment = Minimum = Maximum = 0;
    Length = pRtlIoDecodeMemIoResource(&Descriptor, &Alignment, &Minimum, &Maximum);
    ok(Length == 0x4000, "Memory decoded length 0x%I64x\n", Length);
    ok(Alignment == 0x1000, "Memory decoded alignment 0x%I64x\n", Alignment);
    ok(Minimum == 0x10000000ULL, "Memory decoded minimum 0x%I64x\n", Minimum);
    ok(Maximum == 0x1fffffffULL, "Memory decoded maximum 0x%I64x\n", Maximum);
    Length = pRtlIoDecodeMemIoResource(&Descriptor, NULL, NULL, NULL);
    ok(Length == 0x4000, "Memory decoded length without outputs 0x%I64x\n", Length);

    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Status = pRtlIoEncodeMemIoResource(&Descriptor, CmResourceTypeMemoryLarge, 0x200000000ULL, 0x100000000ULL,
                                       0x100000000ULL, 0xffffffffffULL);
    ok(Status == STATUS_SUCCESS, "Memory40 encode status 0x%08lx\n", Status);
    ok(Descriptor.Type == CmResourceTypeMemoryLarge, "Memory40 type %u\n", Descriptor.Type);
    ok((Descriptor.Flags & CM_RESOURCE_MEMORY_LARGE) == CM_RESOURCE_MEMORY_LARGE_40, "Memory40 flags 0x%x\n",
       Descriptor.Flags);
    ok(Descriptor.u.Memory40.Length40 == 0x2000000, "Memory40 length 0x%lx\n", Descriptor.u.Memory40.Length40);
    ok(Descriptor.u.Memory40.Alignment40 == 0x1000000, "Memory40 alignment 0x%lx\n", Descriptor.u.Memory40.Alignment40);
    Length = pRtlIoDecodeMemIoResource(&Descriptor, &Alignment, &Minimum, &Maximum);
    ok(Length == 0x200000000ULL, "Memory40 decoded length 0x%I64x\n", Length);
    ok(Alignment == 0x100000000ULL, "Memory40 decoded alignment 0x%I64x\n", Alignment);
    ok(Minimum == 0x100000000ULL, "Memory40 decoded minimum 0x%I64x\n", Minimum);
    ok(Maximum == 0xffffffffffULL, "Memory40 decoded maximum 0x%I64x\n", Maximum);

    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Status = pRtlIoEncodeMemIoResource(&Descriptor, CmResourceTypePort, 0x8, 0x8, 0x3f8, 0x3ff);
    ok(Status == STATUS_SUCCESS, "Port encode status 0x%08lx\n", Status);
    ok(Descriptor.Type == CmResourceTypePort, "Port type %u\n", Descriptor.Type);
    ok(Descriptor.u.Port.Length == 0x8, "Port length 0x%lx\n", Descriptor.u.Port.Length);
    ok(Descriptor.u.Port.Alignment == 0x8, "Port alignment 0x%lx\n", Descriptor.u.Port.Alignment);
    Length = pRtlIoDecodeMemIoResource(&Descriptor, &Alignment, &Minimum, &Maximum);
    ok(Length == 0x8, "Port decoded length 0x%I64x\n", Length);
    ok(Minimum == 0x3f8 && Maximum == 0x3ff, "Port decoded range 0x%I64x-0x%I64x\n", Minimum, Maximum);

    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Status = pRtlIoEncodeMemIoResource(&Descriptor, CmResourceTypeMemory, 0x100000001ULL, 0x1000, 0, MAXULONGLONG);
    ok(Status == STATUS_UNSUCCESSFUL, "Unencodable length status 0x%08lx\n", Status);
    Status = pRtlIoEncodeMemIoResource(&Descriptor, CmResourceTypeMemory, 0x100000000ULL, 0x1001, 0, MAXULONGLONG);
    ok(Status == STATUS_UNSUCCESSFUL, "Unencodable alignment status 0x%08lx\n", Status);
    Status = pRtlIoEncodeMemIoResource(&Descriptor, CmResourceTypeDma, 1, 1, 0, 0);
    ok(Status == STATUS_INVALID_PARAMETER, "DMA type status 0x%08lx\n", Status);
}

START_TEST(RtlMemIoResource)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    pRtlCmDecodeMemIoResource = (FN_RtlCmDecodeMemIoResource)GetProcAddress(Ntdll, "RtlCmDecodeMemIoResource");
    pRtlCmEncodeMemIoResource = (FN_RtlCmEncodeMemIoResource)GetProcAddress(Ntdll, "RtlCmEncodeMemIoResource");
    pRtlIoDecodeMemIoResource = (FN_RtlIoDecodeMemIoResource)GetProcAddress(Ntdll, "RtlIoDecodeMemIoResource");
    pRtlIoEncodeMemIoResource = (FN_RtlIoEncodeMemIoResource)GetProcAddress(Ntdll, "RtlIoEncodeMemIoResource");
    if (!pRtlCmDecodeMemIoResource || !pRtlCmEncodeMemIoResource ||
        !pRtlIoDecodeMemIoResource || !pRtlIoEncodeMemIoResource)
    {
        skip("Resource descriptor encoders are not exported\n");
        return;
    }

    TestCmResources();
    TestIoResources();
}
