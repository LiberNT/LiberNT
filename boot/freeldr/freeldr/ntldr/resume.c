/*
 * PROJECT:     LiberNT FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Resume from a hibernation image on BIOS systems
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>
#include "winldr.h"
#include "hiber.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

#define WINLDR_HIBER_PATH          "\\hiberfil.sys"
#define WINLDR_HIBER_READ_SIZE     (1024 * 1024)
#define WINLDR_HIBER_CHUNK_SIZE    (LNT_HIBER_CHUNK_PAGES * PAGE_SIZE)
#define WINLDR_HIBER_MAX_RANGES    65536
#define WINLDR_HIBER_POOL_RUNS     4096
#define WINLDR_HIBER_POOL_EXTRA    64

typedef struct _WINLDR_HIBER
{
    ULONG FileId;
    BOOLEAN FileOpen;
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
    PWINLDR_HIBER_OPERATION_PAGE OperationFirst;
    PWINLDR_HIBER_OPERATION_PAGE OperationLast;
    PULONG64 Borrows;
    ULONG64 BorrowCount;
    ULONG64 BorrowCapacity;
    ULONG64 BorrowRange;
    ULONG64 BorrowPage;
} WINLDR_HIBER;

static WINLDR_HIBER WinLdrHiber;

static
PVOID
WinLdrHiberAllocate(
    _In_ SIZE_T Size)
{
    return MmAllocateMemoryWithType(ALIGN_UP_BY(Size, PAGE_SIZE), LoaderFirmwareTemporary);
}

static
VOID
WinLdrHiberFree(
    _In_opt_ PVOID Buffer,
    _In_ SIZE_T Size)
{
    if (Buffer)
        MmFreeMemoryEx(Buffer, ALIGN_UP_BY(Size, PAGE_SIZE));
}

static
VOID
WinLdrHiberSetType(
    _In_ ULONG64 BasePage,
    _In_ ULONG64 PageCount,
    _In_ TYPE_OF_MEMORY Type)
{
    MmSetMemoryType((PVOID)(ULONG_PTR)(BasePage << PAGE_SHIFT), (SIZE_T)(PageCount << PAGE_SHIFT), Type);
}

static
BOOLEAN
WinLdrHiberIsRam(
    _In_ TYPE_OF_MEMORY Type)
{
    switch (Type)
    {
        case LoaderFirmwarePermanent:
        case LoaderSpecialMemory:
        case LoaderBad:
        case LoaderReserve:
            return FALSE;

        default:
            return TRUE;
    }
}

static
BOOLEAN
WinLdrHiberIsClaimable(
    _In_ PPAGE_LOOKUP_TABLE_ITEM Map,
    _In_ ULONG64 Page)
{
    return Page != 0 && Page < MM_MAX_PAGE_LOADER && Map[Page].PageAllocated == LoaderFree;
}

static
BOOLEAN
WinLdrHiberIsTarget(
    _In_ ULONG64 Page)
{
    return Page < WinLdrHiber.Header->HighestPage && RtlTestBit(&WinLdrHiber.Target, (ULONG)Page);
}

BOOLEAN
WinLdrHiberAllocatePage(
    _Out_ PULONG64 Page)
{
    while (WinLdrHiber.PoolIndex < WinLdrHiber.PoolRuns)
    {
        if (WinLdrHiber.PoolUsed < WinLdrHiber.PoolCount[WinLdrHiber.PoolIndex])
        {
            *Page = WinLdrHiber.PoolBase[WinLdrHiber.PoolIndex] + WinLdrHiber.PoolUsed++;
            return TRUE;
        }

        WinLdrHiber.PoolIndex++;
        WinLdrHiber.PoolUsed = 0;
    }

    return FALSE;
}

static
BOOLEAN
WinLdrHiberCheckMemoryMap(
    _Out_ PULONG64 HighestPage)
{
    PPAGE_LOOKUP_TABLE_ITEM Map;
    PLNT_HIBER_RANGE Range;
    PFN_NUMBER Entries;
    ULONG64 Index;
    ULONG64 Page;

    Map = MmGetMemoryMap(&Entries);
    if (!Map)
        return FALSE;

    for (Index = 0; Index < WinLdrHiber.Header->RangeCount; Index++)
    {
        Range = &WinLdrHiber.Ranges[Index];
        if (Range->BasePage + Range->PageCount > Entries)
            return FALSE;

        for (Page = Range->BasePage; Page < Range->BasePage + Range->PageCount; Page++)
        {
            if (!WinLdrHiberIsRam(Map[Page].PageAllocated))
                return FALSE;
        }
    }

    *HighestPage = max((ULONG64)Entries, WinLdrHiber.Header->HighestPage);
    return TRUE;
}

static
VOID
WinLdrHiberClaimFreeMemory(VOID)
{
    PPAGE_LOOKUP_TABLE_ITEM Map;
    PLNT_HIBER_RANGE Range;
    PFN_NUMBER Entries;
    ULONG64 Index;
    ULONG64 Page;
    ULONG64 Start;
    ULONG64 End;

    Map = MmGetMemoryMap(&Entries);

    for (Index = 0; Index < WinLdrHiber.Header->RangeCount; Index++)
    {
        Range = &WinLdrHiber.Ranges[Index];
        Page = Range->BasePage;
        End = Range->BasePage + Range->PageCount;

        while (Page < End)
        {
            if (!WinLdrHiberIsClaimable(Map, Page))
            {
                Page++;
                continue;
            }

            Start = Page;
            while (Page < End && WinLdrHiberIsClaimable(Map, Page))
                Page++;

            WinLdrHiberSetType(Start, Page - Start, LoaderFirmwareTemporary);
            RtlSetBits(&WinLdrHiber.Owned, (ULONG)Start, (ULONG)(Page - Start));
            WinLdrHiber.OwnedPages += Page - Start;
        }
    }
}

static
VOID
WinLdrHiberClaimPool(
    _In_ ULONG64 Needed)
{
    PPAGE_LOOKUP_TABLE_ITEM Map;
    PFN_NUMBER Entries;
    ULONG64 Page;
    ULONG64 End;

    Map = MmGetMemoryMap(&Entries);
    Page = min((ULONG64)Entries, (ULONG64)MM_MAX_PAGE_LOADER);

    while (Needed != 0 && Page != 0 && WinLdrHiber.PoolRuns < WINLDR_HIBER_POOL_RUNS)
    {
        Page--;
        if (!WinLdrHiberIsClaimable(Map, Page) || WinLdrHiberIsTarget(Page))
            continue;

        End = Page + 1;
        while (End - Page < Needed && WinLdrHiberIsClaimable(Map, Page - 1) && !WinLdrHiberIsTarget(Page - 1))
            Page--;

        WinLdrHiberSetType(Page, End - Page, LoaderFirmwareTemporary);
        WinLdrHiber.PoolBase[WinLdrHiber.PoolRuns] = Page;
        WinLdrHiber.PoolCount[WinLdrHiber.PoolRuns] = End - Page;
        WinLdrHiber.PoolRuns++;
        Needed -= End - Page;
    }
}

static
VOID
WinLdrHiberReleaseMemory(VOID)
{
    ULONG64 Highest = WinLdrHiber.Header->HighestPage;
    ULONG64 Page = 0;
    ULONG64 Start;
    ULONG Index;

    for (Index = 0; Index < WinLdrHiber.PoolRuns; Index++)
        WinLdrHiberSetType(WinLdrHiber.PoolBase[Index], WinLdrHiber.PoolCount[Index], LoaderFree);
    WinLdrHiber.PoolRuns = 0;
    WinLdrHiber.OperationFirst = NULL;
    WinLdrHiber.OperationLast = NULL;

    while (Page < Highest)
    {
        if (!RtlTestBit(&WinLdrHiber.Owned, (ULONG)Page))
        {
            Page++;
            continue;
        }

        Start = Page;
        while (Page < Highest && RtlTestBit(&WinLdrHiber.Owned, (ULONG)Page))
            Page++;

        WinLdrHiberSetType(Start, Page - Start, LoaderFree);
    }

    RtlClearAllBits(&WinLdrHiber.Owned);
    WinLdrHiber.OwnedPages = 0;
}

static
BOOLEAN
WinLdrHiberLzDecompress(
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
WinLdrHiberReadAt(
    _In_ ULONG64 Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    LARGE_INTEGER Position;
    ULONG Count;

    Position.QuadPart = (LONGLONG)Offset;
    if (ArcSeek(WinLdrHiber.FileId, &Position, SeekAbsolute) != ESUCCESS)
        return FALSE;

    return ArcRead(WinLdrHiber.FileId, Buffer, Length, &Count) == ESUCCESS && Count == Length;
}

static
BOOLEAN
WinLdrHiberFill(
    _In_ ULONG Needed)
{
    ULONG Keep = WinLdrHiber.ReadLength - WinLdrHiber.ReadStart;
    ULONG Want;

    if (Keep >= Needed)
        return TRUE;

    RtlMoveMemory(WinLdrHiber.Read, WinLdrHiber.Read + WinLdrHiber.ReadStart, Keep);
    WinLdrHiber.ReadStart = 0;
    WinLdrHiber.ReadLength = Keep;

    Want = (ULONG)min((ULONG64)((WINLDR_HIBER_READ_SIZE - Keep) & ~(PAGE_SIZE - 1)),
                      WinLdrHiber.RecordEnd - WinLdrHiber.ReadNext);
    if (Keep + Want < Needed)
        return FALSE;

    if (Want != 0 && !WinLdrHiberReadAt(WinLdrHiber.ReadNext, WinLdrHiber.Read + Keep, Want))
        return FALSE;

    WinLdrHiber.ReadNext += Want;
    WinLdrHiber.ReadLength += Want;
    return TRUE;
}

static
BOOLEAN
WinLdrHiberBorrowPage(
    _Out_ PULONG64 Page)
{
    PLNT_HIBER_RANGE Range;
    ULONG64 Candidate;

    for (; WinLdrHiber.BorrowRange < WinLdrHiber.Header->RangeCount; WinLdrHiber.BorrowRange++, WinLdrHiber.BorrowPage = 0)
    {
        Range = &WinLdrHiber.Ranges[WinLdrHiber.BorrowRange];
        if (Range->Class != LNT_HIBER_RANGE_ZERO)
            continue;

        for (; WinLdrHiber.BorrowPage < Range->PageCount; WinLdrHiber.BorrowPage++)
        {
            Candidate = Range->BasePage + WinLdrHiber.BorrowPage;
            if (Candidate == 0 ||
                !RtlTestBit(&WinLdrHiber.Owned, (ULONG)Candidate) ||
                RtlTestBit(&WinLdrHiber.Borrowed, (ULONG)Candidate))
            {
                continue;
            }

            if (WinLdrHiber.BorrowCount == WinLdrHiber.BorrowCapacity)
                return FALSE;

            RtlSetBit(&WinLdrHiber.Borrowed, (ULONG)Candidate);
            WinLdrHiber.Borrows[WinLdrHiber.BorrowCount++] = Candidate;
            WinLdrHiber.BorrowPage++;
            *Page = Candidate;
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
WinLdrHiberAddOperation(
    _In_ ULONG64 Destination,
    _In_ ULONG64 Source)
{
    PWINLDR_HIBER_OPERATION_PAGE Page = WinLdrHiber.OperationLast;
    PWINLDR_HIBER_OPERATION_PAGE NewPage;
    ULONG64 Frame;

    if (Page == NULL || Page->Count == WINLDR_HIBER_OPERATIONS_PER_PAGE)
    {
        if (!WinLdrHiberAllocatePage(&Frame))
            return FALSE;

        NewPage = (PWINLDR_HIBER_OPERATION_PAGE)(ULONG_PTR)(Frame << PAGE_SHIFT);
        NewPage->Next = 0;
        NewPage->Count = 0;
        if (Page != NULL)
            Page->Next = Frame << PAGE_SHIFT;
        else
            WinLdrHiber.OperationFirst = NewPage;
        WinLdrHiber.OperationLast = NewPage;
        Page = NewPage;
    }

    Page->Operation[Page->Count].Destination = Destination;
    Page->Operation[Page->Count].Source = Source;
    Page->Count++;
    return TRUE;
}

static
BOOLEAN
WinLdrHiberApply(
    _In_ ULONG64 TargetPage,
    _In_ ULONG PageCount,
    _In_reads_bytes_opt_((SIZE_T)PageCount << PAGE_SHIFT) const UCHAR *Data)
{
    ULONG64 Page;
    ULONG64 Stage;
    ULONG Index;

    if (PageCount == 0 ||
        TargetPage >= WinLdrHiber.Header->HighestPage ||
        PageCount > WinLdrHiber.Header->HighestPage - TargetPage ||
        !RtlAreBitsSet(&WinLdrHiber.Target, (ULONG)TargetPage, PageCount))
    {
        return FALSE;
    }

    for (Index = 0; Index < PageCount; Index++)
    {
        Page = TargetPage + Index;

        if (RtlTestBit(&WinLdrHiber.Owned, (ULONG)Page))
        {
            if (Data)
                RtlCopyMemory((PVOID)(ULONG_PTR)(Page << PAGE_SHIFT), Data + ((SIZE_T)Index << PAGE_SHIFT), PAGE_SIZE);
            else if (!RtlTestBit(&WinLdrHiber.Borrowed, (ULONG)Page))
                RtlZeroMemory((PVOID)(ULONG_PTR)(Page << PAGE_SHIFT), PAGE_SIZE);
            continue;
        }

        Stage = 0;
        if (Data)
        {
            if (!WinLdrHiberAllocatePage(&Stage) && !WinLdrHiberBorrowPage(&Stage))
                return FALSE;
            RtlCopyMemory((PVOID)(ULONG_PTR)(Stage << PAGE_SHIFT), Data + ((SIZE_T)Index << PAGE_SHIFT), PAGE_SIZE);
        }

        if (!WinLdrHiberAddOperation(Page << PAGE_SHIFT, Stage << PAGE_SHIFT))
            return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
WinLdrHiberReadRecords(VOID)
{
    LNT_HIBER_RECORD Record;
    ULONG64 Applied = 0;
    ULONG64 Index;
    ULONG Padded;
    ULONG Percent = 0;
    const UCHAR *Data;

    WinLdrHiber.ReadStart = 0;
    WinLdrHiber.ReadLength = 0;
    WinLdrHiber.ReadNext = WinLdrHiber.Header->RecordOffset;
    WinLdrHiber.RecordEnd = WinLdrHiber.Header->RecordOffset + WinLdrHiber.Header->RecordLength;

    for (;;)
    {
        if (!WinLdrHiberFill(sizeof(Record)))
            return FALSE;

        RtlCopyMemory(&Record, WinLdrHiber.Read + WinLdrHiber.ReadStart, sizeof(Record));
        if (Record.Type == LNT_HIBER_RECORD_END)
            break;

        if (Record.DataLength > WINLDR_HIBER_CHUNK_SIZE)
            return FALSE;

        Padded = ALIGN_UP_BY(Record.DataLength, 8);
        if (!WinLdrHiberFill(sizeof(Record) + Padded))
            return FALSE;

        Data = WinLdrHiber.Read + WinLdrHiber.ReadStart + sizeof(Record);

        switch (Record.Type)
        {
            case LNT_HIBER_RECORD_ZERO:
                if (Record.DataLength != 0 || !WinLdrHiberApply(Record.TargetPage, Record.PageCount, NULL))
                    return FALSE;
                break;

            case LNT_HIBER_RECORD_RAW:
                if (Record.PageCount > LNT_HIBER_CHUNK_PAGES ||
                    Record.DataLength != Record.PageCount * PAGE_SIZE ||
                    !WinLdrHiberApply(Record.TargetPage, Record.PageCount, Data))
                {
                    return FALSE;
                }
                break;

            case LNT_HIBER_RECORD_LZ:
                if (Record.PageCount > LNT_HIBER_CHUNK_PAGES ||
                    !WinLdrHiberLzDecompress(Data, Record.DataLength, WinLdrHiber.Chunk, Record.PageCount * PAGE_SIZE) ||
                    !WinLdrHiberApply(Record.TargetPage, Record.PageCount, WinLdrHiber.Chunk))
                {
                    return FALSE;
                }
                break;

            default:
                return FALSE;
        }

        WinLdrHiber.ReadStart += sizeof(Record) + Padded;
        Applied += Record.PageCount;
        if (Applied * 100 / WinLdrHiber.Header->TargetPages > Percent)
        {
            Percent = (ULONG)(Applied * 100 / WinLdrHiber.Header->TargetPages);
            UiUpdateProgressBar(Percent, NULL);
        }
    }

    if (Applied != WinLdrHiber.Header->TargetPages)
        return FALSE;

    for (Index = 0; Index < WinLdrHiber.BorrowCount; Index++)
    {
        if (!WinLdrHiberAddOperation(WinLdrHiber.Borrows[Index] << PAGE_SHIFT, 0))
            return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
WinLdrHiberValidateHeader(VOID)
{
    PLNT_HIBER_HEADER Header = WinLdrHiber.Header;
    ULONG Crc = Header->HeaderCrc;
    BOOLEAN Valid;

    Header->HeaderCrc = 0;
    Valid = (RtlComputeCrc32(0, (PUCHAR)Header, sizeof(LNT_HIBER_HEADER)) == Crc);
    Header->HeaderCrc = Crc;

    return Valid &&
           Header->Version == LNT_HIBER_VERSION &&
           Header->HeaderSize == sizeof(LNT_HIBER_HEADER) &&
           Header->RangeCount != 0 &&
           Header->RangeCount <= WINLDR_HIBER_MAX_RANGES &&
           Header->RangeTableOffset >= PAGE_SIZE &&
           Header->RangeTableOffset <= WinLdrHiber.FileSize &&
           Header->RangeCount * sizeof(LNT_HIBER_RANGE) <= WinLdrHiber.FileSize - Header->RangeTableOffset &&
           Header->RecordOffset <= WinLdrHiber.FileSize &&
           Header->RecordLength <= WinLdrHiber.FileSize - Header->RecordOffset &&
           Header->HighestPage != 0 &&
           Header->HighestPage <= MAXULONG &&
           Header->TargetPages != 0 &&
           Header->LowStubPhysical != 0 &&
           Header->LowStubPhysical < 0x100000000ULL &&
           Header->LowStubProt32Offset < Header->LowStubPages * PAGE_SIZE &&
           Header->LowStubGdtOffset < Header->LowStubPages * PAGE_SIZE &&
           Header->RuntimeRangeCount == 0;
}

static
BOOLEAN
WinLdrHiberValidateRanges(VOID)
{
    PLNT_HIBER_HEADER Header = WinLdrHiber.Header;
    PLNT_HIBER_RANGE Range;
    ULONG64 NextPage = 0;
    ULONG64 Target = 0;
    ULONG64 Data = 0;
    ULONG64 Index;

    for (Index = 0; Index < Header->RangeCount; Index++)
    {
        Range = &WinLdrHiber.Ranges[Index];
        if (Range->PageCount == 0 ||
            Range->BasePage < NextPage ||
            Range->BasePage + Range->PageCount > Header->HighestPage ||
            (Range->Class != LNT_HIBER_RANGE_DATA && Range->Class != LNT_HIBER_RANGE_ZERO))
        {
            return FALSE;
        }

        RtlSetBits(&WinLdrHiber.Target, (ULONG)Range->BasePage, Range->PageCount);
        NextPage = Range->BasePage + Range->PageCount;
        Target += Range->PageCount;
        if (Range->Class == LNT_HIBER_RANGE_DATA)
            Data += Range->PageCount;
    }

    return Target == Header->TargetPages && Data == Header->DataPages;
}

static
SIZE_T
WinLdrHiberBitmapSize(
    _In_ ULONG64 Bits)
{
    return (SIZE_T)ALIGN_UP_BY((Bits + 31) / 32 * sizeof(ULONG), PAGE_SIZE);
}

static
PVOID
WinLdrHiberAllocateBitmap(
    _Out_ PRTL_BITMAP Bitmap,
    _In_ ULONG64 Bits)
{
    PULONG Buffer = WinLdrHiberAllocate(WinLdrHiberBitmapSize(Bits));

    if (!Buffer)
        return NULL;

    RtlInitializeBitMap(Bitmap, Buffer, (ULONG)Bits);
    RtlClearAllBits(Bitmap);
    return Buffer;
}

static
VOID
WinLdrHiberFreeBuffers(VOID)
{
    ULONG64 Highest = WinLdrHiber.Header ? WinLdrHiber.Header->HighestPage : 0;
    SIZE_T BitmapSize = WinLdrHiberBitmapSize(Highest);

    WinLdrHiberFree(WinLdrHiber.Borrows, (SIZE_T)(WinLdrHiber.BorrowCapacity * sizeof(ULONG64)));
    WinLdrHiberFree(WinLdrHiber.PoolBase, WINLDR_HIBER_POOL_RUNS * sizeof(ULONG64));
    WinLdrHiberFree(WinLdrHiber.PoolCount, WINLDR_HIBER_POOL_RUNS * sizeof(ULONG64));
    WinLdrHiberFree(WinLdrHiber.Chunk, WINLDR_HIBER_CHUNK_SIZE);
    WinLdrHiberFree(WinLdrHiber.Read, WINLDR_HIBER_READ_SIZE);
    WinLdrHiberFree(WinLdrHiber.Borrowed.Buffer, BitmapSize);
    WinLdrHiberFree(WinLdrHiber.Owned.Buffer, BitmapSize);
    WinLdrHiberFree(WinLdrHiber.Target.Buffer, BitmapSize);
    if (WinLdrHiber.Ranges)
        WinLdrHiberFree(WinLdrHiber.Ranges, (SIZE_T)(WinLdrHiber.Header->RangeCount * sizeof(LNT_HIBER_RANGE)));
    WinLdrHiberFree(WinLdrHiber.Header, PAGE_SIZE);
    if (WinLdrHiber.FileOpen)
        ArcClose(WinLdrHiber.FileId);

    RtlZeroMemory(&WinLdrHiber, sizeof(WinLdrHiber));
}

VOID
WinLdrResumeFromHibernation(
    _In_ PCSTR BootPath)
{
    FILEINFORMATION Information;
    PLNT_HIBER_HEADER Header;
    CHAR Path[MAX_PATH];
    PCSTR DeviceEnd;
    ULONG64 HighestPage;
    ULONG64 Pending;
    SIZE_T Size;

    RtlZeroMemory(&WinLdrHiber, sizeof(WinLdrHiber));

    DeviceEnd = strrchr(BootPath, ')');
    if (!DeviceEnd)
        return;

    Size = (SIZE_T)(DeviceEnd + 1 - BootPath);
    if (Size + sizeof(WINLDR_HIBER_PATH) > sizeof(Path))
        return;
    RtlCopyMemory(Path, BootPath, Size);
    RtlCopyMemory(Path + Size, WINLDR_HIBER_PATH, sizeof(WINLDR_HIBER_PATH));

    if (ArcOpen(Path, OpenReadOnly, &WinLdrHiber.FileId) != ESUCCESS)
        return;
    WinLdrHiber.FileOpen = TRUE;

    if (ArcGetFileInformation(WinLdrHiber.FileId, &Information) != ESUCCESS)
        goto Cleanup;
    WinLdrHiber.FileSize = (ULONG64)Information.EndingAddress.QuadPart;
    if (WinLdrHiber.FileSize < 2 * PAGE_SIZE)
        goto Cleanup;

    WinLdrHiber.Header = WinLdrHiberAllocate(PAGE_SIZE);
    if (!WinLdrHiber.Header || !WinLdrHiberReadAt(0, WinLdrHiber.Header, PAGE_SIZE))
        goto Cleanup;

    Header = WinLdrHiber.Header;
    if (Header->Signature != LNT_HIBER_SIGNATURE)
        goto Cleanup;

    UiDrawProgressBarCenter("Resuming from hibernation...");

    if (!WinLdrHiberValidateHeader())
    {
        ERR("The hibernation image header is not valid\n");
        goto Cleanup;
    }

    Size = (SIZE_T)(Header->RangeCount * sizeof(LNT_HIBER_RANGE));
    WinLdrHiber.BorrowCapacity = Header->TargetPages;
    WinLdrHiber.Ranges = WinLdrHiberAllocate(Size);
    if (!WinLdrHiber.Ranges ||
        !WinLdrHiberAllocateBitmap(&WinLdrHiber.Target, Header->HighestPage) ||
        !WinLdrHiberAllocateBitmap(&WinLdrHiber.Owned, Header->HighestPage) ||
        !WinLdrHiberAllocateBitmap(&WinLdrHiber.Borrowed, Header->HighestPage) ||
        !(WinLdrHiber.Read = WinLdrHiberAllocate(WINLDR_HIBER_READ_SIZE)) ||
        !(WinLdrHiber.Chunk = WinLdrHiberAllocate(WINLDR_HIBER_CHUNK_SIZE)) ||
        !(WinLdrHiber.PoolBase = WinLdrHiberAllocate(WINLDR_HIBER_POOL_RUNS * sizeof(ULONG64))) ||
        !(WinLdrHiber.PoolCount = WinLdrHiberAllocate(WINLDR_HIBER_POOL_RUNS * sizeof(ULONG64))) ||
        !(WinLdrHiber.Borrows = WinLdrHiberAllocate((SIZE_T)(WinLdrHiber.BorrowCapacity * sizeof(ULONG64)))))
    {
        ERR("Not enough memory to resume from hibernation\n");
        goto Cleanup;
    }

    if (!WinLdrHiberReadAt(Header->RangeTableOffset, WinLdrHiber.Ranges, (ULONG)Size) ||
        !WinLdrHiberValidateRanges())
    {
        ERR("The hibernation image memory ranges are not valid\n");
        goto Cleanup;
    }

    if (!WinLdrHiberCheckMemoryMap(&HighestPage))
    {
        ERR("The memory map changed since the system hibernated\n");
        goto Cleanup;
    }

    WinLdrHiberClaimFreeMemory();

    Pending = Header->TargetPages - WinLdrHiber.OwnedPages;
    WinLdrHiberClaimPool(Pending + Pending / WINLDR_HIBER_OPERATIONS_PER_PAGE + WINLDR_HIBER_POOL_EXTRA);

    if (!WinLdrArchPrepareResume(Header, HighestPage))
    {
        ERR("The system cannot resume from hibernation on this platform\n");
        goto Release;
    }

    if (!WinLdrHiberReadRecords())
    {
        ERR("The hibernation image could not be read\n");
        goto Release;
    }

    ArcClose(WinLdrHiber.FileId);
    WinLdrHiber.FileOpen = FALSE;

    WinLdrArchResume(WinLdrHiber.OperationFirst ? (ULONG_PTR)WinLdrHiber.OperationFirst : 0);

Release:
    WinLdrHiberReleaseMemory();
Cleanup:
    WinLdrHiberFreeBuffers();
}
