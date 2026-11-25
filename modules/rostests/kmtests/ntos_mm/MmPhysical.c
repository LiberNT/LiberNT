/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel-Mode Test Suite physical memory query API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'hPmK'

static
VOID
TestPhysicalAddress(VOID)
{
    PVOID Buffer;
    PHYSICAL_ADDRESS Phys;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_TEST);
    ok(Buffer != NULL, "no pool\n");
    if (Buffer == NULL) return;

    RtlFillMemory(Buffer, PAGE_SIZE, 0x77);

    Phys = MmGetPhysicalAddress(Buffer);
    ok(Phys.QuadPart != 0, "physical address is zero\n");
    ok((Phys.QuadPart & (PAGE_SIZE - 1)) == ((ULONG_PTR)Buffer & (PAGE_SIZE - 1)), "page offset mismatch\n");

    ExFreePoolWithTag(Buffer, TAG_TEST);
}

static
VOID
TestAddressValidity(VOID)
{
    PVOID Buffer;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, 64, TAG_TEST);
    ok(Buffer != NULL, "no pool\n");
    if (Buffer != NULL)
    {
        ok_bool_true(MmIsAddressValid(Buffer), "nonpaged pool valid");
        ExFreePoolWithTag(Buffer, TAG_TEST);
    }

    ok_bool_false(MmIsAddressValid(NULL), "NULL valid");
    ok_bool_false(MmIsAddressValid((PVOID)(ULONG_PTR)0x10), "page zero valid");
}

static
VOID
TestMdlForNonPagedPool(VOID)
{
    PVOID Buffer;
    PMDL Mdl;
    PVOID SystemVa;
    SIZE_T MdlSize;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, 3 * PAGE_SIZE, TAG_TEST);
    ok(Buffer != NULL, "no pool\n");
    if (Buffer == NULL) return;

    MdlSize = MmSizeOfMdl(Buffer, 3 * PAGE_SIZE);
    ok(MdlSize >= sizeof(MDL) + 3 * sizeof(PFN_NUMBER), "MmSizeOfMdl %Iu too small\n", MdlSize);

    Mdl = IoAllocateMdl(Buffer, 3 * PAGE_SIZE, FALSE, FALSE, NULL);
    ok(Mdl != NULL, "IoAllocateMdl failed\n");
    if (Mdl != NULL)
    {
        MmBuildMdlForNonPagedPool(Mdl);
        ok(Mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL, "MDL flags %x\n", Mdl->MdlFlags);

        SystemVa = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        ok_eq_pointer(SystemVa, Buffer);

        ok_eq_ulong(MmGetMdlByteCount(Mdl), 3UL * PAGE_SIZE);
        ok_eq_pointer(MmGetMdlVirtualAddress(Mdl), Buffer);

        IoFreeMdl(Mdl);
    }

    ExFreePoolWithTag(Buffer, TAG_TEST);
}

static
VOID
TestAllocatePagesForMdl(VOID)
{
    PHYSICAL_ADDRESS Low, High, Skip;
    PMDL Mdl;
    PVOID Mapped;

    Low.QuadPart = 0;
    High.QuadPart = -1;
    Skip.QuadPart = 0;

    Mdl = MmAllocatePagesForMdl(Low, High, Skip, 2 * PAGE_SIZE);
    ok(Mdl != NULL, "MmAllocatePagesForMdl failed\n");
    if (Mdl == NULL) return;

    ok(MmGetMdlByteCount(Mdl) <= 2 * PAGE_SIZE, "byte count %lu\n", MmGetMdlByteCount(Mdl));
    ok(MmGetMdlByteCount(Mdl) > 0, "byte count zero\n");

    Mapped = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    ok(Mapped != NULL, "map failed\n");
    if (Mapped != NULL)
    {
        RtlFillMemory(Mapped, MmGetMdlByteCount(Mdl), 0x3C);
        ok(*(PUCHAR)Mapped == 0x3C, "readback failed\n");
        MmUnmapLockedPages(Mapped, Mdl);
    }

    MmFreePagesFromMdl(Mdl);
    ExFreePool(Mdl);
}

static
PMDL
AllocateInHighestRange(
    _Out_ PPHYSICAL_ADDRESS Low,
    _Out_ PPHYSICAL_ADDRESS High)
{
    PPHYSICAL_MEMORY_RANGE Ranges;
    PHYSICAL_ADDRESS Skip, Below;
    PMDL Mdl = NULL;
    ULONG Index, Best;

    Ranges = MmGetPhysicalMemoryRanges();
    ok(Ranges != NULL, "MmGetPhysicalMemoryRanges failed\n");
    if (Ranges == NULL)
        return NULL;

    Skip.QuadPart = 0;
    Below.QuadPart = -1;
    for (;;)
    {
        Best = MAXULONG;
        for (Index = 0; Ranges[Index].BaseAddress.QuadPart != 0 || Ranges[Index].NumberOfBytes.QuadPart != 0; Index++)
        {
            if ((ULONGLONG)Ranges[Index].BaseAddress.QuadPart < (ULONGLONG)Below.QuadPart &&
                (Best == MAXULONG || Ranges[Index].BaseAddress.QuadPart > Ranges[Best].BaseAddress.QuadPart))
            {
                Best = Index;
            }
        }
        if (Best == MAXULONG)
            break;

        *Low = Ranges[Best].BaseAddress;
        High->QuadPart = Ranges[Best].BaseAddress.QuadPart + Ranges[Best].NumberOfBytes.QuadPart - 1;
        Below = Ranges[Best].BaseAddress;
        Mdl = MmAllocatePagesForMdlEx(*Low, *High, Skip, 16 * PAGE_SIZE, MmCached, MM_DONT_ZERO_ALLOCATION);
        if (Mdl != NULL && MmGetMdlByteCount(Mdl) != 0)
            break;
        if (Mdl != NULL)
        {
            ExFreePool(Mdl);
            Mdl = NULL;
        }
    }

    ExFreePool(Ranges);
    return Mdl;
}

static
VOID
TestHighestRangeMappable(VOID)
{
    PHYSICAL_ADDRESS Low, High;
    PFN_NUMBER *Pages;
    PMDL Mdl;
    PUCHAR Mapped;
    ULONG Index, Count;

    Mdl = AllocateInHighestRange(&Low, &High);
    if (skip(Mdl != NULL, "no physical range has a free page\n"))
        return;
    trace("highest physical range with free pages %I64x-%I64x\n", Low.QuadPart, High.QuadPart);

    Pages = MmGetMdlPfnArray(Mdl);
    Count = MmGetMdlByteCount(Mdl) >> PAGE_SHIFT;
    for (Index = 0; Index < Count; Index++)
    {
        ok(((ULONG64)Pages[Index] << PAGE_SHIFT) >= (ULONG64)Low.QuadPart &&
           ((ULONG64)Pages[Index] << PAGE_SHIFT) <= (ULONG64)High.QuadPart,
           "page %Ix is outside %I64x-%I64x\n", Pages[Index], Low.QuadPart, High.QuadPart);
    }

    Mapped = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    ok(Mapped != NULL, "pages of the highest physical range could not be mapped\n");
    if (Mapped != NULL)
    {
        for (Index = 0; Index < Count; Index++)
            Mapped[Index << PAGE_SHIFT] = (UCHAR)(0x5A ^ Index);
        for (Index = 0; Index < Count; Index++)
        {
            ok(Mapped[Index << PAGE_SHIFT] == (UCHAR)(0x5A ^ Index), "page %lu read back %02x\n", Index,
               Mapped[Index << PAGE_SHIFT]);
        }
        MmUnmapLockedPages(Mapped, Mdl);
    }

    MmFreePagesFromMdl(Mdl);
    ExFreePool(Mdl);
}

static
VOID
TestCopyMemory(VOID)
{
    UCHAR SystemSource[64];
    UCHAR Target[64];
    MM_COPY_ADDRESS SourceAddress;
    PVOID UserSource;
    PVOID FreeBase;
    SIZE_T BytesTransferred;
    SIZE_T RegionSize;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < RTL_NUMBER_OF(SystemSource); ++Index)
        SystemSource[Index] = (UCHAR)(Index ^ 0xA5);

    RtlZeroMemory(Target, sizeof(Target));
    SourceAddress.VirtualAddress = SystemSource;
    BytesTransferred = 0;
    Status = MmCopyMemory(Target, SourceAddress, sizeof(Target), MM_COPY_MEMORY_VIRTUAL, &BytesTransferred);
    trace("MmCopyMemory(system virtual) returned 0x%08lx, bytes %Iu\n", Status, BytesTransferred);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_size(BytesTransferred, sizeof(Target));
    ok(RtlEqualMemory(Target, SystemSource, sizeof(Target)), "system virtual copy contents differed\n");

    UserSource = NULL;
    RegionSize = PAGE_SIZE;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &UserSource, 0, &RegionSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    trace("user source allocation returned 0x%08lx, base %p, size %Iu\n", Status, UserSource, RegionSize);
    if (skip(NT_SUCCESS(Status), "could not allocate the user source page\n"))
        return;

    RtlCopyMemory(UserSource, SystemSource, sizeof(SystemSource));
    RtlZeroMemory(Target, sizeof(Target));
    SourceAddress.VirtualAddress = UserSource;
    BytesTransferred = 0;
    Status = MmCopyMemory(Target, SourceAddress, sizeof(Target), MM_COPY_MEMORY_VIRTUAL, &BytesTransferred);
    trace("MmCopyMemory(user virtual) returned 0x%08lx, bytes %Iu\n", Status, BytesTransferred);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_size(BytesTransferred, sizeof(Target));
    ok(RtlEqualMemory(Target, SystemSource, sizeof(Target)), "user virtual copy contents differed\n");

    FreeBase = UserSource;
    RegionSize = 0;
    Status = ZwFreeVirtualMemory(NtCurrentProcess(), &FreeBase, &RegionSize, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

START_TEST(MmPhysical)
{
    TestPhysicalAddress();
    TestAddressValidity();
    TestMdlForNonPagedPool();
    TestAllocatePagesForMdl();
    TestHighestRangeMappable();
    TestCopyMemory();
}
