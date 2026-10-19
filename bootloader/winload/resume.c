/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Resume from a hibernation image
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"

#define OSL_HIBER_PATH          "\\hiberfil.sys"
#define OSL_HIBER_READ_SIZE     (1024 * 1024)
#define OSL_HIBER_CHUNK_SIZE    (LNT_HIBER_CHUNK_PAGES * PAGE_SIZE)
#define OSL_HIBER_MAX_RANGES    65536
#define OSL_HIBER_POOL_RUNS     4096
#define OSL_HIBER_POOL_EXTRA    64

typedef struct _OSL_HIBER
{
    PVOID File;
    ULONG64 FileSize;
    PLNT_HIBER_HEADER Header;
    PLNT_HIBER_RANGE Ranges;
    RTL_BITMAP Target;
    RTL_BITMAP Owned;
    RTL_BITMAP Borrowed;
    ULONG64 OwnedPages;
    PUCHAR Read;
    ULONG ReadStart;
    ULONG ReadLength;
    ULONG64 ReadNext;
    ULONG64 RecordEnd;
    PUCHAR Chunk;
    PULONG64 PoolBase;
    PULONG64 PoolCount;
    ULONG PoolRuns;
    ULONG PoolIndex;
    ULONG64 PoolUsed;
    POSL_HIBER_OPERATION_PAGE OperationFirst;
    POSL_HIBER_OPERATION_PAGE OperationLast;
    PULONG64 Borrows;
    ULONG64 BorrowCount;
    ULONG64 BorrowCapacity;
    ULONG64 BorrowRange;
    ULONG64 BorrowPage;
} OSL_HIBER;

static OSL_HIBER OslHiber;

VOID
OslHiberMarkOwned(
    _In_ ULONG64 BasePage,
    _In_ ULONG64 PageCount)
{
    RtlSetBits(&OslHiber.Owned, (ULONG)BasePage, (ULONG)PageCount);
    OslHiber.OwnedPages += PageCount;
}

VOID
OslHiberAddPoolRun(
    _In_ ULONG64 BasePage,
    _In_ ULONG64 PageCount)
{
    if (OslHiber.PoolRuns == OSL_HIBER_POOL_RUNS)
        return;

    OslHiber.PoolBase[OslHiber.PoolRuns] = BasePage;
    OslHiber.PoolCount[OslHiber.PoolRuns] = PageCount;
    OslHiber.PoolRuns++;
}

BOOLEAN
OslHiberAllocatePage(
    _Out_ PULONG64 Page)
{
    while (OslHiber.PoolIndex < OslHiber.PoolRuns)
    {
        if (OslHiber.PoolUsed < OslHiber.PoolCount[OslHiber.PoolIndex])
        {
            *Page = OslHiber.PoolBase[OslHiber.PoolIndex] + OslHiber.PoolUsed++;
            return TRUE;
        }

        OslHiber.PoolIndex++;
        OslHiber.PoolUsed = 0;
    }

    return FALSE;
}

static
VOID
OslHiberReleaseMemory(VOID)
{
    ULONG64 Highest = OslHiber.Header->HighestPage;
    ULONG64 Page = 0;
    ULONG64 Start;
    ULONG Index;

    for (Index = 0; Index < OslHiber.PoolRuns; Index++)
    {
        FwFreePages((PVOID)(ULONG_PTR)(OslHiber.PoolBase[Index] << PAGE_SHIFT),
                    (SIZE_T)(OslHiber.PoolCount[Index] << PAGE_SHIFT));
    }
    OslHiber.PoolRuns = 0;
    OslHiber.OperationFirst = NULL;
    OslHiber.OperationLast = NULL;

    while (Page < Highest)
    {
        if (!RtlTestBit(&OslHiber.Owned, (ULONG)Page))
        {
            Page++;
            continue;
        }

        Start = Page;
        while (Page < Highest && RtlTestBit(&OslHiber.Owned, (ULONG)Page))
            Page++;

        FwFreePages((PVOID)(ULONG_PTR)(Start << PAGE_SHIFT), (SIZE_T)((Page - Start) << PAGE_SHIFT));
    }

    RtlClearAllBits(&OslHiber.Owned);
    OslHiber.OwnedPages = 0;
}

static
BOOLEAN
OslHiberLzDecompress(
    _In_reads_bytes_(Length) const UCHAR *Source,
    _In_ ULONG Length,
    _Out_writes_bytes_(Expected) PUCHAR Destination,
    _In_ ULONG Expected)
{
    const UCHAR *Ip = Source;
    const UCHAR *IpEnd = Source + Length;
    PUCHAR Op = Destination;
    PUCHAR OpEnd = Destination + Expected;
    const UCHAR *Match;
    ULONG Literal;
    ULONG MatchLength;
    ULONG Offset;
    UCHAR Token;
    UCHAR Extra;

    while (Ip < IpEnd)
    {
        Token = *Ip++;

        Literal = Token >> 4;
        if (Literal == 15)
        {
            do
            {
                if (Ip >= IpEnd)
                    return FALSE;
                Extra = *Ip++;
                Literal += Extra;
            } while (Extra == 255);
        }

        if ((ULONG_PTR)(IpEnd - Ip) < Literal || (ULONG_PTR)(OpEnd - Op) < Literal)
            return FALSE;
        RtlCopyMemory(Op, Ip, Literal);
        Ip += Literal;
        Op += Literal;

        if (Ip == IpEnd)
            break;

        if ((ULONG_PTR)(IpEnd - Ip) < 2)
            return FALSE;
        Offset = Ip[0] | ((ULONG)Ip[1] << 8);
        Ip += 2;

        MatchLength = Token & 15;
        if (MatchLength == 15)
        {
            do
            {
                if (Ip >= IpEnd)
                    return FALSE;
                Extra = *Ip++;
                MatchLength += Extra;
            } while (Extra == 255);
        }
        MatchLength += 4;

        if (Offset == 0 || (ULONG_PTR)(Op - Destination) < Offset || (ULONG_PTR)(OpEnd - Op) < MatchLength)
            return FALSE;

        Match = Op - Offset;
        if (Offset >= MatchLength)
        {
            RtlCopyMemory(Op, Match, MatchLength);
            Op += MatchLength;
        }
        else
        {
            while (MatchLength-- != 0)
                *Op++ = *Match++;
        }
    }

    return Op == OpEnd;
}

static
BOOLEAN
OslHiberFill(
    _In_ ULONG Needed)
{
    ULONG Keep = OslHiber.ReadLength - OslHiber.ReadStart;
    ULONG Want;

    if (Keep >= Needed)
        return TRUE;

    RtlMoveMemory(OslHiber.Read, OslHiber.Read + OslHiber.ReadStart, Keep);
    OslHiber.ReadStart = 0;
    OslHiber.ReadLength = Keep;

    Want = (ULONG)min((ULONG64)((OSL_HIBER_READ_SIZE - Keep) & ~(PAGE_SIZE - 1)),
                      OslHiber.RecordEnd - OslHiber.ReadNext);
    if (Keep + Want < Needed)
        return FALSE;

    if (Want != 0 && !FsReadFileAt(OslHiber.File, OslHiber.ReadNext, OslHiber.Read + Keep, Want))
        return FALSE;

    OslHiber.ReadNext += Want;
    OslHiber.ReadLength += Want;
    return TRUE;
}

static
BOOLEAN
OslHiberBorrowPage(
    _Out_ PULONG64 Page)
{
    PLNT_HIBER_RANGE Range;
    ULONG64 Candidate;

    for (; OslHiber.BorrowRange < OslHiber.Header->RangeCount; OslHiber.BorrowRange++, OslHiber.BorrowPage = 0)
    {
        Range = &OslHiber.Ranges[OslHiber.BorrowRange];
        if (Range->Class != LNT_HIBER_RANGE_ZERO)
            continue;

        for (; OslHiber.BorrowPage < Range->PageCount; OslHiber.BorrowPage++)
        {
            Candidate = Range->BasePage + OslHiber.BorrowPage;
            if (Candidate == 0 ||
                !RtlTestBit(&OslHiber.Owned, (ULONG)Candidate) ||
                RtlTestBit(&OslHiber.Borrowed, (ULONG)Candidate))
            {
                continue;
            }

            if (OslHiber.BorrowCount == OslHiber.BorrowCapacity)
                return FALSE;

            RtlSetBit(&OslHiber.Borrowed, (ULONG)Candidate);
            OslHiber.Borrows[OslHiber.BorrowCount++] = Candidate;
            OslHiber.BorrowPage++;
            *Page = Candidate;
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
OslHiberAddOperation(
    _In_ ULONG64 Destination,
    _In_ ULONG64 Source)
{
    POSL_HIBER_OPERATION_PAGE Page = OslHiber.OperationLast;
    POSL_HIBER_OPERATION_PAGE NewPage;
    ULONG64 Frame;

    if (Page == NULL || Page->Count == OSL_HIBER_OPERATIONS_PER_PAGE)
    {
        if (!OslHiberAllocatePage(&Frame))
            return FALSE;

        NewPage = (POSL_HIBER_OPERATION_PAGE)(ULONG_PTR)(Frame << PAGE_SHIFT);
        NewPage->Next = 0;
        NewPage->Count = 0;
        if (Page != NULL)
            Page->Next = Frame << PAGE_SHIFT;
        else
            OslHiber.OperationFirst = NewPage;
        OslHiber.OperationLast = NewPage;
        Page = NewPage;
    }

    Page->Operation[Page->Count].Destination = Destination;
    Page->Operation[Page->Count].Source = Source;
    Page->Count++;
    return TRUE;
}

static
BOOLEAN
OslHiberApply(
    _In_ ULONG64 TargetPage,
    _In_ ULONG PageCount,
    _In_reads_bytes_opt_((SIZE_T)PageCount << PAGE_SHIFT) const UCHAR *Data)
{
    ULONG64 Page;
    ULONG64 Stage;
    ULONG Index;

    if (PageCount == 0 ||
        TargetPage >= OslHiber.Header->HighestPage ||
        PageCount > OslHiber.Header->HighestPage - TargetPage ||
        !RtlAreBitsSet(&OslHiber.Target, (ULONG)TargetPage, PageCount))
    {
        return FALSE;
    }

    for (Index = 0; Index < PageCount; Index++)
    {
        Page = TargetPage + Index;

        if (RtlTestBit(&OslHiber.Owned, (ULONG)Page))
        {
            if (Data)
                RtlCopyMemory((PVOID)(ULONG_PTR)(Page << PAGE_SHIFT), Data + ((SIZE_T)Index << PAGE_SHIFT), PAGE_SIZE);
            else if (!RtlTestBit(&OslHiber.Borrowed, (ULONG)Page))
                RtlZeroMemory((PVOID)(ULONG_PTR)(Page << PAGE_SHIFT), PAGE_SIZE);
            continue;
        }

        Stage = 0;
        if (Data)
        {
            if (!OslHiberAllocatePage(&Stage) && !OslHiberBorrowPage(&Stage))
                return FALSE;
            RtlCopyMemory((PVOID)(ULONG_PTR)(Stage << PAGE_SHIFT), Data + ((SIZE_T)Index << PAGE_SHIFT), PAGE_SIZE);
        }

        if (!OslHiberAddOperation(Page << PAGE_SHIFT, Stage << PAGE_SHIFT))
            return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
OslHiberReadRecords(VOID)
{
    LNT_HIBER_RECORD Record;
    ULONG64 Applied = 0;
    ULONG64 Index;
    ULONG Padded;
    ULONG Tenth = 0;
    const UCHAR *Data;

    OslHiber.ReadStart = 0;
    OslHiber.ReadLength = 0;
    OslHiber.ReadNext = OslHiber.Header->RecordOffset;
    OslHiber.RecordEnd = OslHiber.Header->RecordOffset + OslHiber.Header->RecordLength;

    for (;;)
    {
        if (!OslHiberFill(sizeof(Record)))
            return FALSE;

        RtlCopyMemory(&Record, OslHiber.Read + OslHiber.ReadStart, sizeof(Record));
        if (Record.Type == LNT_HIBER_RECORD_END)
            break;

        if (Record.DataLength > OSL_HIBER_CHUNK_SIZE)
            return FALSE;

        Padded = ALIGN_UP_BY(Record.DataLength, 8);
        if (!OslHiberFill(sizeof(Record) + Padded))
            return FALSE;

        Data = OslHiber.Read + OslHiber.ReadStart + sizeof(Record);

        switch (Record.Type)
        {
            case LNT_HIBER_RECORD_ZERO:
                if (Record.DataLength != 0 || !OslHiberApply(Record.TargetPage, Record.PageCount, NULL))
                    return FALSE;
                break;

            case LNT_HIBER_RECORD_RAW:
                if (Record.PageCount > LNT_HIBER_CHUNK_PAGES ||
                    Record.DataLength != Record.PageCount * PAGE_SIZE ||
                    !OslHiberApply(Record.TargetPage, Record.PageCount, Data))
                {
                    return FALSE;
                }
                break;

            case LNT_HIBER_RECORD_LZ:
                if (Record.PageCount > LNT_HIBER_CHUNK_PAGES ||
                    !OslHiberLzDecompress(Data, Record.DataLength, OslHiber.Chunk, Record.PageCount * PAGE_SIZE) ||
                    !OslHiberApply(Record.TargetPage, Record.PageCount, OslHiber.Chunk))
                {
                    return FALSE;
                }
                break;

            default:
                return FALSE;
        }

        OslHiber.ReadStart += sizeof(Record) + Padded;
        Applied += Record.PageCount;
        if (Applied * 10 / OslHiber.Header->TargetPages > Tenth)
        {
            Tenth = (ULONG)(Applied * 10 / OslHiber.Header->TargetPages);
            FwPrint(L"Resuming from hibernation: %lu%%\r\n", Tenth * 10);
        }
    }

    if (Applied != OslHiber.Header->TargetPages)
        return FALSE;

    for (Index = 0; Index < OslHiber.BorrowCount; Index++)
    {
        if (!OslHiberAddOperation(OslHiber.Borrows[Index] << PAGE_SHIFT, 0))
            return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
OslHiberValidateHeader(VOID)
{
    PLNT_HIBER_HEADER Header = OslHiber.Header;
    ULONG Crc = Header->HeaderCrc;
    BOOLEAN Valid;

    Header->HeaderCrc = 0;
    Valid = (RtlComputeCrc32(0, (PUCHAR)Header, sizeof(LNT_HIBER_HEADER)) == Crc);
    Header->HeaderCrc = Crc;

    return Valid &&
           Header->Version == LNT_HIBER_VERSION &&
           Header->HeaderSize == sizeof(LNT_HIBER_HEADER) &&
           Header->RangeCount != 0 &&
           Header->RangeCount <= OSL_HIBER_MAX_RANGES &&
           Header->RangeTableOffset >= PAGE_SIZE &&
           Header->RangeTableOffset <= OslHiber.FileSize &&
           Header->RangeCount * sizeof(LNT_HIBER_RANGE) <= OslHiber.FileSize - Header->RangeTableOffset &&
           Header->RecordOffset <= OslHiber.FileSize &&
           Header->RecordLength <= OslHiber.FileSize - Header->RecordOffset &&
           Header->HighestPage != 0 &&
           Header->HighestPage <= MAXULONG &&
           Header->TargetPages != 0 &&
           Header->LowStubPhysical != 0 &&
           Header->LowStubPhysical < 0x100000000ULL &&
           Header->LowStubProt32Offset < Header->LowStubPages * PAGE_SIZE &&
           Header->LowStubGdtOffset < Header->LowStubPages * PAGE_SIZE &&
           Header->RuntimeRangeCount <= LNT_HIBER_MAX_RUNTIME_RANGES;
}

static
BOOLEAN
OslHiberValidateRanges(VOID)
{
    PLNT_HIBER_HEADER Header = OslHiber.Header;
    PLNT_HIBER_RANGE Range;
    ULONG64 NextPage = 0;
    ULONG64 Target = 0;
    ULONG64 Data = 0;
    ULONG64 Index;

    for (Index = 0; Index < Header->RangeCount; Index++)
    {
        Range = &OslHiber.Ranges[Index];
        if (Range->PageCount == 0 ||
            Range->BasePage < NextPage ||
            Range->BasePage + Range->PageCount > Header->HighestPage ||
            (Range->Class != LNT_HIBER_RANGE_DATA && Range->Class != LNT_HIBER_RANGE_ZERO))
        {
            return FALSE;
        }

        RtlSetBits(&OslHiber.Target, (ULONG)Range->BasePage, Range->PageCount);
        NextPage = Range->BasePage + Range->PageCount;
        Target += Range->PageCount;
        if (Range->Class == LNT_HIBER_RANGE_DATA)
            Data += Range->PageCount;
    }

    return Target == Header->TargetPages && Data == Header->DataPages;
}

static
SIZE_T
OslHiberBitmapSize(
    _In_ ULONG64 Bits)
{
    return (SIZE_T)ALIGN_UP_BY((Bits + 31) / 32 * sizeof(ULONG), PAGE_SIZE);
}

static
PVOID
OslHiberAllocateBitmap(
    _Out_ PRTL_BITMAP Bitmap,
    _In_ ULONG64 Bits)
{
    PULONG Buffer = FwAllocatePages(OslHiberBitmapSize(Bits));

    if (!Buffer)
        return NULL;

    RtlInitializeBitMap(Bitmap, Buffer, (ULONG)Bits);
    RtlClearAllBits(Bitmap);
    return Buffer;
}

static
VOID
OslHiberFreeBuffers(VOID)
{
    ULONG64 Highest = OslHiber.Header ? OslHiber.Header->HighestPage : 0;
    SIZE_T BitmapSize = OslHiberBitmapSize(Highest);

    if (OslHiber.Borrows)
        FwFreePages(OslHiber.Borrows, (SIZE_T)ALIGN_UP_BY(OslHiber.BorrowCapacity * sizeof(ULONG64), PAGE_SIZE));
    if (OslHiber.PoolBase)
        FwFreePages(OslHiber.PoolBase, OSL_HIBER_POOL_RUNS * sizeof(ULONG64));
    if (OslHiber.PoolCount)
        FwFreePages(OslHiber.PoolCount, OSL_HIBER_POOL_RUNS * sizeof(ULONG64));
    if (OslHiber.Chunk)
        FwFreePages(OslHiber.Chunk, OSL_HIBER_CHUNK_SIZE);
    if (OslHiber.Read)
        FwFreePages(OslHiber.Read, OSL_HIBER_READ_SIZE);
    if (OslHiber.Borrowed.Buffer)
        FwFreePages(OslHiber.Borrowed.Buffer, BitmapSize);
    if (OslHiber.Owned.Buffer)
        FwFreePages(OslHiber.Owned.Buffer, BitmapSize);
    if (OslHiber.Target.Buffer)
        FwFreePages(OslHiber.Target.Buffer, BitmapSize);
    if (OslHiber.Ranges)
        FwFreePages(OslHiber.Ranges, (SIZE_T)ALIGN_UP_BY(OslHiber.Header->RangeCount * sizeof(LNT_HIBER_RANGE), PAGE_SIZE));
    if (OslHiber.Header)
        FwFreePages(OslHiber.Header, PAGE_SIZE);
    if (OslHiber.File)
        FsCloseFile(OslHiber.File);

    RtlZeroMemory(&OslHiber, sizeof(OslHiber));
}

VOID
OslResumeFromHibernation(
    _In_ PDK_DEVICE OsDevice)
{
    PLNT_HIBER_HEADER Header;
    ULONG64 HighestPage;
    ULONG64 Pending;
    SIZE_T Size;

    RtlZeroMemory(&OslHiber, sizeof(OslHiber));

    OslHiber.File = FsOpenFileA(OsDevice, OSL_HIBER_PATH, &OslHiber.FileSize);
    if (!OslHiber.File || OslHiber.FileSize < 2 * PAGE_SIZE)
        goto Cleanup;

    OslHiber.Header = FwAllocatePages(PAGE_SIZE);
    if (!OslHiber.Header || !FsReadFileAt(OslHiber.File, 0, OslHiber.Header, PAGE_SIZE))
        goto Cleanup;

    Header = OslHiber.Header;
    if (Header->Signature != LNT_HIBER_SIGNATURE)
        goto Cleanup;

    FwPrint(L"Resuming from hibernation...\r\n");

    if (!OslHiberValidateHeader())
    {
        FwPrint(L"The hibernation image header is not valid.\r\n");
        goto Cleanup;
    }

    Size = (SIZE_T)(Header->RangeCount * sizeof(LNT_HIBER_RANGE));
    OslHiber.BorrowCapacity = Header->TargetPages;
    OslHiber.Ranges = FwAllocatePages(ALIGN_UP_BY(Size, PAGE_SIZE));
    if (!OslHiber.Ranges ||
        !OslHiberAllocateBitmap(&OslHiber.Target, Header->HighestPage) ||
        !OslHiberAllocateBitmap(&OslHiber.Owned, Header->HighestPage) ||
        !OslHiberAllocateBitmap(&OslHiber.Borrowed, Header->HighestPage) ||
        !(OslHiber.Read = FwAllocatePages(OSL_HIBER_READ_SIZE)) ||
        !(OslHiber.Chunk = FwAllocatePages(OSL_HIBER_CHUNK_SIZE)) ||
        !(OslHiber.PoolBase = FwAllocatePages(OSL_HIBER_POOL_RUNS * sizeof(ULONG64))) ||
        !(OslHiber.PoolCount = FwAllocatePages(OSL_HIBER_POOL_RUNS * sizeof(ULONG64))) ||
        !(OslHiber.Borrows = FwAllocatePages(ALIGN_UP_BY(OslHiber.BorrowCapacity * sizeof(ULONG64), PAGE_SIZE))))
    {
        FwPrint(L"Not enough memory to resume from hibernation.\r\n");
        goto Cleanup;
    }

    if (!FsReadFileAt(OslHiber.File, Header->RangeTableOffset, OslHiber.Ranges, (ULONG)Size) ||
        !OslHiberValidateRanges())
    {
        FwPrint(L"The hibernation image memory ranges are not valid.\r\n");
        goto Cleanup;
    }

    if (!FwHiberCheckMemoryMap(Header, OslHiber.Ranges, Header->RangeCount, &HighestPage))
    {
        FwPrint(L"The memory map changed since the system hibernated.\r\n");
        goto Cleanup;
    }

    FwHiberClaimFreeMemory(OslHiber.Ranges, Header->RangeCount);

    Pending = Header->TargetPages - OslHiber.OwnedPages;
    FwHiberClaimPool(Pending + Pending / OSL_HIBER_OPERATIONS_PER_PAGE + OSL_HIBER_POOL_EXTRA);

    if (!OslArchPrepareResume(Header, HighestPage))
    {
        FwPrint(L"The system cannot resume from hibernation on this platform.\r\n");
        goto Release;
    }

    if (!OslHiberReadRecords())
    {
        FwPrint(L"The hibernation image could not be read.\r\n");
        goto Release;
    }

    FsCloseFile(OslHiber.File);
    OslHiber.File = NULL;

    if (!FwHiberExitBootServices(Header))
    {
        FwPrint(L"The firmware runtime services could not be restored.\r\n");
        FwHalt();
    }

    OslArchResume(OslHiber.OperationFirst ? (ULONG_PTR)OslHiber.OperationFirst : 0);

Release:
    OslHiberReleaseMemory();
Cleanup:
    OslHiberFreeBuffers();
}
