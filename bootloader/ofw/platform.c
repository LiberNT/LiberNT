/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Open Firmware entry point and firmware services
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <ofw.h>
#include <drivers/bootvid/framebuf.h>

#define OFW_PAGE_SIZE        0x1000
#define OFW_HEAP_CHUNK       0x100000
#define OFW_HEAP_SPLIT       64
#define OFW_MAX_DISKS        8
#define OFW_MAX_RANGES       32
#define OFW_MAX_DESCRIPTORS  (OFW_MAX_ALLOCATIONS + 4 * OFW_MAX_RANGES + 8)
#define OFW_PRINT_CHARS      512
#define OFW_DEVICE_PATH      128
#define OFW_TREE_BUDGET      256
#define OFW_MBR_TABLE        0x1BE
#define OFW_MBR_ENTRY_SIZE   16
#define OFW_MBR_PARTITIONS   4
#define OFW_MBR_SIGNATURE    0x1B8
#define OFW_VECTOR_PAGES     4
#define OFW_CDROM_BLOCK      2048
#define OFW_ISO_DESCRIPTOR   (16 * OFW_CDROM_BLOCK)

#define PREP_ISA_IO_BASE     0x80000000UL
#define PREP_PCI_MEMORY_BASE 0xC0000000UL
#define PREP_PCI_DMA_OFFSET  0x80000000UL
#define PREP_COM1_PORT       0x3F8
#define PREP_TIMEBASE        25000000

#define OFW_DISPLAY_TYPE_LENGTH    7
#define OFW_DISPLAY_PATH           256
#define OFW_DISPLAY_DEPTH          32
#define OFW_PCI_REG_CELLS          5
#define OFW_PCI_MAX_BARS           6
#define OFW_PCI_MEMORY_LIMIT       0x3F000000
#define OFW_BOCHS_VENDOR           0x1234
#define OFW_BOCHS_DEVICE           0x1111
#define OFW_BOCHS_ENDIAN_REVISION  2
#define OFW_BOCHS_MMIO_BAR         0x18
#define OFW_BOCHS_ENDIAN_REGISTER  0x604
#define OFW_BOCHS_LITTLE_ENDIAN    0x1E1E1E1E
#define OFW_MASK_RED               0x00FF0000
#define OFW_MASK_GREEN             0x0000FF00
#define OFW_MASK_BLUE              0x000000FF
#define OFW_MASK_RESERVED          0xFF000000

typedef struct _OFW_DISK
{
    OFW_HANDLE Ihandle;
    ULONG BlockSize;
    ULONGLONG BlockCount;
    BOOLEAN IsCdrom;
} OFW_DISK, *POFW_DISK;

typedef VOID (*POFW_APPLICATION_ENTRY)(PVOID Argument);

const USHORT FwApplicationSubsystem = IMAGE_SUBSYSTEM_NATIVE;

POFW_APPLICATION_PARAMETERS OfwParameters;
OFW_MACHINE_INFO OfwMachine;
VOID (*FwLateDebugPutChar)(UCHAR Character);

static OFW_APPLICATION_PARAMETERS OfwRootParameters;
static POFW_SHARED_STATE OfwShared;
static OFW_DISK OfwDisks[OFW_MAX_DISKS];
static ULONG OfwDiskCount;
static BOOLEAN OfwDisksScanned;
static FREELDR_MEMORY_DESCRIPTOR OfwMap[OFW_MAX_DESCRIPTORS + 1];
static ULONG OfwMapCount;

static const PCWSTR OfwStorePaths[] =
{
    L"\\EFI\\LiberNT\\Boot\\BCD",
    L"\\Boot\\BCD",
};

static
VOID
OfwWriteText(
    _In_reads_(Length) PCSTR Text,
    _In_ ULONG Length)
{
    if (OfwAvailable && OfwStdout != OFW_INVALID)
        OfwWrite(OfwStdout, Text, Length);
}

VOID
FwPrint(
    _In_ PCWSTR Format,
    ...)
{
    WCHAR Text[OFW_PRINT_CHARS];
    CHAR Narrow[OFW_PRINT_CHARS];
    va_list Arguments;
    ULONG i;

    va_start(Arguments, Format);
    RtlStringCchVPrintfW(Text, RTL_NUMBER_OF(Text), Format, Arguments);
    va_end(Arguments);

    for (i = 0; Text[i] != UNICODE_NULL; i++)
        Narrow[i] = Text[i] <= 0x7F ? (CHAR)Text[i] : '?';

    OfwWriteText(Narrow, i);
}

VOID
FwDebugPrint(
    _In_ PCSTR Text)
{
    for (; *Text; Text++)
    {
        if (!OfwAvailable)
        {
            if (FwLateDebugPutChar)
            {
                if (*Text == '\n')
                    FwLateDebugPutChar('\r');
                FwLateDebugPutChar((UCHAR)*Text);
            }
            continue;
        }

        if (*Text == '\n')
            OfwWriteText("\r", 1);
        OfwWriteText(Text, 1);
    }
}

static
BOOLEAN
OfwRecordAllocation(
    _In_ ULONG Base,
    _In_ ULONG Size)
{
    if (OfwShared->AllocationCount >= OFW_MAX_ALLOCATIONS)
        return FALSE;

    OfwShared->Allocations[OfwShared->AllocationCount].Base = Base;
    OfwShared->Allocations[OfwShared->AllocationCount].Size = Size;
    OfwShared->AllocationCount++;
    return TRUE;
}

static
VOID
OfwRelease(
    _In_ ULONG Base,
    _In_ ULONG Size)
{
    ULONG Cells[2] = {Base, Size};

    OfwCall("release", 2, 0, Cells);
}

static
ULONG
OfwReadRanges(
    _In_z_ PCSTR Property,
    _Out_writes_(OFW_MAX_RANGES * 2) PULONG Ranges)
{
    OFW_HANDLE Memory = OfwFindDevice("/memory");
    LONG Length;
    ULONG Count, i;

    if (Memory == OFW_INVALID)
        return 0;

    Length = OfwGetProp(Memory, Property, Ranges, OFW_MAX_RANGES * 2 * sizeof(ULONG));
    if (Length <= 0)
        return 0;

    Count = (ULONG)Length / (2 * sizeof(ULONG));
    for (i = 0; i < Count * 2; i++)
        Ranges[i] = OfwCellToHost(Ranges[i]);

    return Count;
}

static
BOOLEAN
OfwOverlapsAllocation(
    _In_ ULONG Base,
    _In_ ULONG Size,
    _Out_ PULONG Below)
{
    ULONG i;

    for (i = 0; i < OfwShared->AllocationCount; i++)
    {
        if (Base < OfwShared->Allocations[i].Base + OfwShared->Allocations[i].Size &&
            OfwShared->Allocations[i].Base < Base + Size)
        {
            *Below = OfwShared->Allocations[i].Base;
            return TRUE;
        }
    }

    return FALSE;
}

PVOID
FwAllocatePagesBelow(
    _In_ SIZE_T Size,
    _In_ ULONGLONG MaxAddress)
{
    ULONG Ranges[OFW_MAX_RANGES * 2];
    ULONG Count, i, Base, End, Top, Candidate, Below;

    Size = ALIGN_UP_BY(Size, OFW_PAGE_SIZE);
    if (Size == 0 || !OfwAvailable)
        return NULL;

    Count = OfwReadRanges("available", Ranges);
    for (i = Count; i > 0; i--)
    {
        Base = Ranges[(i - 1) * 2];
        End = Base + Ranges[(i - 1) * 2 + 1];
        Top = (ULONG)min((ULONGLONG)End, MaxAddress);

        while (Top >= Base + Size && Top - Size >= Base)
        {
            Candidate = (Top - (ULONG)Size) & ~(OFW_PAGE_SIZE - 1);
            if (Candidate < Base)
                break;

            if (OfwOverlapsAllocation(Candidate, (ULONG)Size, &Below))
            {
                Top = Below;
                continue;
            }

            if (OfwClaim(Candidate, (ULONG)Size, 0) == Candidate)
            {
                if (!OfwRecordAllocation(Candidate, (ULONG)Size))
                {
                    OfwRelease(Candidate, (ULONG)Size);
                    return NULL;
                }

                return (PVOID)(ULONG_PTR)Candidate;
            }

            Top = Candidate;
        }
    }

    return NULL;
}

PVOID
FwAllocatePages(
    _In_ SIZE_T Size)
{
    return FwAllocatePagesBelow(Size, MAXULONG);
}

BOOLEAN
FwAllocatePagesAt(
    _In_ PVOID Address,
    _In_ SIZE_T Size)
{
    ULONG Base = (ULONG)(ULONG_PTR)Address;
    ULONG Below;

    Size = ALIGN_UP_BY(Size, OFW_PAGE_SIZE);
    if (OfwOverlapsAllocation(Base, (ULONG)Size, &Below) || OfwClaim(Base, (ULONG)Size, 0) != Base)
        return FALSE;

    if (!OfwRecordAllocation(Base, (ULONG)Size))
    {
        OfwRelease(Base, (ULONG)Size);
        return FALSE;
    }

    return TRUE;
}

VOID
FwFreePages(
    _In_ PVOID Memory,
    _In_ SIZE_T Size)
{
    ULONG Base = (ULONG)(ULONG_PTR)Memory;
    ULONG i;

    for (i = 0; i < OfwShared->AllocationCount; i++)
    {
        if (OfwShared->Allocations[i].Base != Base)
            continue;

        if (OfwAvailable)
            OfwRelease(Base, OfwShared->Allocations[i].Size);

        OfwShared->Allocations[i] = OfwShared->Allocations[--OfwShared->AllocationCount];
        return;
    }

    UNREFERENCED_PARAMETER(Size);
}

PVOID
FwAllocate(
    _In_ SIZE_T Size)
{
    POFW_HEAP_BLOCK Block, Last = NULL, Split;
    ULONG Chunk;

    Size = ALIGN_UP_BY(max(Size, (SIZE_T)1), sizeof(OFW_HEAP_BLOCK));

    for (Block = OfwShared->Heap; Block; Last = Block, Block = Block->Next)
    {
        if (!Block->Free || Block->Size < Size)
            continue;

        if (Block->Size >= Size + sizeof(OFW_HEAP_BLOCK) + OFW_HEAP_SPLIT)
        {
            Split = (POFW_HEAP_BLOCK)((PUCHAR)(Block + 1) + Size);
            Split->Size = Block->Size - (ULONG)Size - sizeof(OFW_HEAP_BLOCK);
            Split->Free = TRUE;
            Split->Next = Block->Next;
            Block->Size = (ULONG)Size;
            Block->Next = Split;
        }

        Block->Free = FALSE;
        return Block + 1;
    }

    Chunk = (ULONG)max((SIZE_T)OFW_HEAP_CHUNK, ALIGN_UP_BY(Size + sizeof(OFW_HEAP_BLOCK), OFW_PAGE_SIZE));
    Block = FwAllocatePages(Chunk);
    if (!Block)
        return NULL;

    Block->Size = Chunk - sizeof(OFW_HEAP_BLOCK);
    Block->Free = TRUE;
    Block->Next = NULL;
    if (Last)
        Last->Next = Block;
    else
        OfwShared->Heap = Block;

    return FwAllocate(Size);
}

VOID
FwFree(
    _In_ PVOID Memory)
{
    POFW_HEAP_BLOCK Block, Next;

    if (!Memory)
        return;

    Block = (POFW_HEAP_BLOCK)Memory - 1;
    Block->Free = TRUE;

    for (Next = Block->Next;
         Next && Next->Free && (PUCHAR)(Block + 1) + Block->Size == (PUCHAR)Next;
         Next = Block->Next)
    {
        Block->Size += sizeof(OFW_HEAP_BLOCK) + Next->Size;
        Block->Next = Next->Next;
    }
}

static
VOID
OfwPaint(
    _In_ ULONG BasePage,
    _In_ ULONG PageCount,
    _In_ TYPE_OF_MEMORY Type)
{
    FREELDR_MEMORY_DESCRIPTOR Pieces[OFW_MAX_DESCRIPTORS + 1];
    ULONG Count = 0, i, Start, End, CutStart, CutEnd;

    if (PageCount == 0)
        return;

    End = BasePage + PageCount;
    for (i = 0; i < OfwMapCount; i++)
    {
        Start = OfwMap[i].BasePage;
        CutEnd = OfwMap[i].BasePage + OfwMap[i].PageCount;
        CutStart = max(Start, BasePage);

        if (CutEnd <= BasePage || Start >= End || Count + 2 > OFW_MAX_DESCRIPTORS)
        {
            Pieces[Count++] = OfwMap[i];
            continue;
        }

        if (Start < BasePage)
        {
            Pieces[Count] = OfwMap[i];
            Pieces[Count++].PageCount = BasePage - Start;
        }

        if (CutEnd > End)
        {
            Pieces[Count] = OfwMap[i];
            Pieces[Count].BasePage = End;
            Pieces[Count++].PageCount = CutEnd - End;
        }

        UNREFERENCED_PARAMETER(CutStart);
    }

    if (Count < OFW_MAX_DESCRIPTORS)
    {
        Pieces[Count].BasePage = BasePage;
        Pieces[Count].PageCount = PageCount;
        Pieces[Count++].MemoryType = Type;
    }

    for (i = 1; i < Count; i++)
    {
        FREELDR_MEMORY_DESCRIPTOR Entry = Pieces[i];
        ULONG j = i;

        while (j > 0 && Pieces[j - 1].BasePage > Entry.BasePage)
        {
            Pieces[j] = Pieces[j - 1];
            j--;
        }

        Pieces[j] = Entry;
    }

    RtlCopyMemory(OfwMap, Pieces, Count * sizeof(Pieces[0]));
    OfwMapCount = Count;
    RtlZeroMemory(&OfwMap[OfwMapCount], sizeof(OfwMap[0]));
}

static
VOID
OfwPaintBytes(
    _In_ ULONG Base,
    _In_ ULONG Size,
    _In_ TYPE_OF_MEMORY Type,
    _In_ BOOLEAN Inner)
{
    ULONG First, Last;

    if (Size == 0)
        return;

    if (Inner)
    {
        First = (Base + OFW_PAGE_SIZE - 1) / OFW_PAGE_SIZE;
        Last = (ULONG)(((ULONGLONG)Base + Size) / OFW_PAGE_SIZE);
    }
    else
    {
        First = Base / OFW_PAGE_SIZE;
        Last = (ULONG)(((ULONGLONG)Base + Size + OFW_PAGE_SIZE - 1) / OFW_PAGE_SIZE);
    }

    if (Last > First)
        OfwPaint(First, Last - First, Type);
}

PFREELDR_MEMORY_DESCRIPTOR
FwGetMemoryMap(
    _Out_ PULONG Count)
{
    ULONG Ranges[OFW_MAX_RANGES * 2];
    ULONG Ranges2[OFW_MAX_RANGES * 2];
    ULONG RangeCount, i;

    OfwMapCount = 0;
    RtlZeroMemory(OfwMap, sizeof(OfwMap));

    RangeCount = OfwReadRanges("reg", Ranges);
    for (i = 0; i < RangeCount; i++)
        OfwPaintBytes(Ranges[i * 2], Ranges[i * 2 + 1], LoaderFirmwarePermanent, FALSE);

    RangeCount = OfwReadRanges("available", Ranges2);
    for (i = 0; i < RangeCount; i++)
        OfwPaintBytes(Ranges2[i * 2], Ranges2[i * 2 + 1], LoaderFree, TRUE);

    OfwPaintBytes(OfwStage0Info->Stage0Base, OfwStage0Info->Stage0Size, LoaderFirmwareTemporary, FALSE);
    OfwPaintBytes(OfwStage0Info->ImageBase, OfwStage0Info->ImageSize, LoaderLoadedProgram, FALSE);
    if (OfwStage0Info->InitrdSize)
        OfwPaintBytes(OfwStage0Info->InitrdBase, OfwStage0Info->InitrdSize, LoaderFirmwareTemporary, FALSE);

    for (i = 0; i < OfwShared->AllocationCount; i++)
    {
        OfwPaintBytes(OfwShared->Allocations[i].Base,
                      OfwShared->Allocations[i].Size,
                      LoaderLoadedProgram,
                      FALSE);
    }

    OfwPaint(0, OFW_VECTOR_PAGES, LoaderFirmwarePermanent);

    *Count = OfwMapCount;
    return OfwMap;
}

static
VOID
OfwProbeDisk(
    _In_ OFW_HANDLE Node)
{
    CHAR Path[OFW_DEVICE_PATH];
    UCHAR Signature[8];
    POFW_DISK Disk;
    ULONG Value;

    if (OfwDiskCount >= OFW_MAX_DISKS || OfwPackageToPath(Node, Path, sizeof(Path)) <= 0)
        return;

    Disk = &OfwDisks[OfwDiskCount];
    RtlZeroMemory(Disk, sizeof(*Disk));

    Disk->Ihandle = OfwOpen(Path);
    if (Disk->Ihandle == OFW_INVALID)
        return;

    Disk->BlockSize = 512;
    if (OfwCallMethod1("block-size", Disk->Ihandle, &Value) == 0 && Value != 0 && (Value & (Value - 1)) == 0)
        Disk->BlockSize = Value;

    if (OfwCallMethod1("#blocks", Disk->Ihandle, &Value) == 0)
        Disk->BlockCount = Value;

    if (OfwSeek(Disk->Ihandle, OFW_ISO_DESCRIPTOR) >= 0 &&
        OfwRead(Disk->Ihandle, Signature, sizeof(Signature)) == sizeof(Signature) &&
        RtlEqualMemory(&Signature[1], "CD001", 5))
    {
        Disk->IsCdrom = TRUE;
        if (Disk->BlockCount != 0 && Disk->BlockSize != OFW_CDROM_BLOCK)
            Disk->BlockCount = Disk->BlockCount * Disk->BlockSize / OFW_CDROM_BLOCK;
        Disk->BlockSize = OFW_CDROM_BLOCK;
    }

    OfwDiskCount++;
}

static
VOID
OfwWalkTree(
    _In_ OFW_HANDLE Node,
    _Inout_ PULONG Budget)
{
    CHAR Type[16];
    OFW_HANDLE Child;
    LONG Length;

    for (; Node != OFW_INVALID && *Budget != 0; Node = OfwPeer(Node))
    {
        (*Budget)--;

        Length = OfwGetProp(Node, "device_type", Type, sizeof(Type) - 1);
        if (Length > 0)
        {
            Type[Length] = ANSI_NULL;
            if (strcmp(Type, "block") == 0)
                OfwProbeDisk(Node);
        }

        Child = OfwChild(Node);
        if (Child != OFW_INVALID)
            OfwWalkTree(Child, Budget);
    }
}

static
VOID
OfwScanDisks(VOID)
{
    ULONG Budget = OFW_TREE_BUDGET;

    if (OfwDisksScanned)
        return;

    OfwDisksScanned = TRUE;
    OfwWalkTree(OfwChild(OfwPeer(OFW_INVALID)), &Budget);
}

BOOLEAN
FwGetDisk(
    _In_ ULONG Index,
    _Out_ PDK_DEVICE Disk)
{
    OfwScanDisks();

    if (Index >= OfwDiskCount)
        return FALSE;

    RtlZeroMemory(Disk, sizeof(*Disk));
    Disk->Firmware = &OfwDisks[Index];
    Disk->Length = OfwDisks[Index].BlockCount ? OfwDisks[Index].BlockCount * OfwDisks[Index].BlockSize
                                              : MAXULONGLONG;
    Disk->BlockSize = OfwDisks[Index].BlockSize;
    Disk->DiskIndex = Index;
    return TRUE;
}

BOOLEAN
FwRescanDisks(VOID)
{
    return FALSE;
}

BOOLEAN
FwReadBlocks(
    _In_ PVOID Firmware,
    _In_ ULONGLONG Block,
    _In_ ULONG Count,
    _Out_ PVOID Buffer)
{
    POFW_DISK Disk = Firmware;
    ULONG Length = Count * Disk->BlockSize;

    return OfwAvailable &&
           OfwSeek(Disk->Ihandle, Block * Disk->BlockSize) >= 0 &&
           OfwRead(Disk->Ihandle, Buffer, Length) == (LONG)Length;
}

BOOLEAN
FwBufferAligned(
    _In_ PVOID Firmware,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Firmware);
    UNREFERENCED_PARAMETER(Buffer);
    return TRUE;
}

static
BOOLEAN
OfwOpenPartition(
    _In_ ULONG DiskIndex,
    _In_ ULONG PartitionNumber,
    _Out_ PDK_DEVICE Partition)
{
    UCHAR Sector[512];
    PUCHAR Entry;
    DK_DEVICE Disk;
    ULONG Index, Number;

    if (!FwGetDisk(DiskIndex, &Disk))
        return FALSE;

    if (PartitionNumber == 0)
    {
        *Partition = Disk;
        return TRUE;
    }

    if (!DkRead(&Disk, 0, sizeof(Sector), Sector) || Sector[510] != 0x55 || Sector[511] != 0xAA)
        return FALSE;

    for (Index = 0, Number = 0; Index < OFW_MBR_PARTITIONS; Index++)
    {
        Entry = Sector + OFW_MBR_TABLE + Index * OFW_MBR_ENTRY_SIZE;
        if (Entry[4] == 0)
            continue;

        if (++Number != PartitionNumber)
            continue;

        *Partition = Disk;
        Partition->Offset = (ULONGLONG)LeUlong(Entry + 8) * Disk.BlockSize;
        Partition->Length = (ULONGLONG)LeUlong(Entry + 12) * Disk.BlockSize;
        Partition->PartitionNumber = Number;
        return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
OfwHasStore(
    _In_ PDK_DEVICE Partition)
{
    PVOID Data;
    ULONG Size, i;

    for (i = 0; i < RTL_NUMBER_OF(OfwStorePaths); i++)
    {
        Data = FsReadFile(Partition, OfwStorePaths[i], &Size);
        if (Data)
        {
            FwFreePages(Data, Size);
            return TRUE;
        }
    }

    return FALSE;
}

static
VOID
OfwFindBootDevice(VOID)
{
    DK_DEVICE Partition;
    ULONG Disk, Number;

    for (Disk = 0; FwGetDisk(Disk, &Partition); Disk++)
    {
        for (Number = 1; Number <= OFW_MBR_PARTITIONS; Number++)
        {
            if (OfwOpenPartition(Disk, Number, &Partition) && OfwHasStore(&Partition))
            {
                OfwParameters->BootDisk = Disk;
                OfwParameters->BootPartition = Number;
                return;
            }
        }
    }

    OfwParameters->BootDisk = MAXULONG;
}

BOOLEAN
FwGetBootDevice(
    _Out_ PDK_DEVICE Device)
{
    if (OfwParameters->BootDisk == MAXULONG)
        return FALSE;

    return OfwOpenPartition(OfwParameters->BootDisk, OfwParameters->BootPartition, Device);
}

BOOLEAN
FwGetBootPartition(
    _Out_ PULONG PartitionNumber,
    _Out_writes_bytes_(16) PUCHAR Signature,
    _Out_ PBOOLEAN IsGpt)
{
    UCHAR Sector[512];
    DK_DEVICE Disk;

    if (OfwParameters->BootDisk == MAXULONG ||
        !FwGetDisk(OfwParameters->BootDisk, &Disk) ||
        !DkRead(&Disk, 0, sizeof(Sector), Sector))
    {
        return FALSE;
    }

    RtlZeroMemory(Signature, 16);
    RtlCopyMemory(Signature, Sector + OFW_MBR_SIGNATURE, sizeof(ULONG));
    *PartitionNumber = OfwParameters->BootPartition;
    *IsGpt = FALSE;
    return TRUE;
}

PVOID
FwReadBootFile(
    _In_ PCWSTR Path,
    _Out_ PULONG Size)
{
    DK_DEVICE Device;

    *Size = 0;

    if (!FwGetBootDevice(&Device))
        return NULL;

    return FsReadFile(&Device, Path, Size);
}

BOOLEAN
FwGetBootMedium(
    _Out_ PDK_DEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);
    return FALSE;
}

PVOID
FwDownloadFile(
    _In_ PCSTR Url,
    _In_opt_ PCSTR Address,
    _Out_ PULONG Size)
{
    UNREFERENCED_PARAMETER(Url);
    UNREFERENCED_PARAMETER(Address);
    *Size = 0;
    return NULL;
}

PVOID
FwGetAcpiRootPointer(VOID)
{
    return NULL;
}

PVOID
FwGetSmbiosEntryPoint(VOID)
{
    return NULL;
}

static
OFW_HANDLE
OfwFindDisplay(
    _In_ OFW_HANDLE Node)
{
    OFW_HANDLE Child, Display;
    CHAR Type[16];
    LONG Length;

    Length = OfwGetProp(Node, "device_type", Type, sizeof(Type));
    if (Length >= OFW_DISPLAY_TYPE_LENGTH && RtlEqualMemory(Type, "display", OFW_DISPLAY_TYPE_LENGTH))
        return Node;

    for (Child = OfwChild(Node); Child != OFW_INVALID; Child = OfwPeer(Child))
    {
        Display = OfwFindDisplay(Child);
        if (Display != OFW_INVALID)
            return Display;
    }

    return OFW_INVALID;
}

static
BOOLEAN
OfwTranslate(
    _In_ ULONG Virtual,
    _Out_ PULONG Physical)
{
    ULONG Cells[7];
    ULONG Mmu;

    if (!OfwGetPropCell(OfwFindDevice("/chosen"), "mmu", &Mmu))
        return FALSE;

    Cells[0] = (ULONG)(ULONG_PTR)"translate";
    Cells[1] = Mmu;
    Cells[2] = Virtual;
    if (OfwCall("call-method", 3, 4, Cells) != 0 || Cells[3] != 0 || Cells[4] == 0)
        return FALSE;

    *Physical = Cells[6];
    return TRUE;
}

static
BOOLEAN
OfwSetDisplayByteOrder(
    _In_ OFW_HANDLE Display)
{
    ULONG Vendor, Device, Revision, Registers[OFW_PCI_REG_CELLS * OFW_PCI_MAX_BARS];
    ULONG Index, Base, Physical;
    volatile ULONG* Control;
    LONG Length;

    if (OfwMachine.MachineType != PPC_MACHINE_PREP ||
        !OfwGetPropCell(Display, "vendor-id", &Vendor) || Vendor != OFW_BOCHS_VENDOR ||
        !OfwGetPropCell(Display, "device-id", &Device) || Device != OFW_BOCHS_DEVICE ||
        !OfwGetPropCell(Display, "revision-id", &Revision) || Revision < OFW_BOCHS_ENDIAN_REVISION)
    {
        return FALSE;
    }

    Length = OfwGetProp(Display, "assigned-addresses", Registers, sizeof(Registers));
    if (Length <= 0 || Length > (LONG)sizeof(Registers))
        return FALSE;

    for (Index = 0; Index + OFW_PCI_REG_CELLS <= (ULONG)Length / sizeof(ULONG); Index += OFW_PCI_REG_CELLS)
    {
        if ((OfwCellToHost(Registers[Index]) & 0xFF) == OFW_BOCHS_MMIO_BAR &&
            OfwCellToHost(Registers[Index + 4]) >= OFW_BOCHS_ENDIAN_REGISTER + sizeof(ULONG))
        {
            break;
        }
    }

    if (Index + OFW_PCI_REG_CELLS > (ULONG)Length / sizeof(ULONG))
        return FALSE;

    Base = OfwCellToHost(Registers[Index + 2]);
    if (OfwCellToHost(Registers[Index + 1]) != 0 || Base >= OFW_PCI_MEMORY_LIMIT)
        return FALSE;

    Base += OfwMachine.PciMemoryPhysicalBase;
    if (!OfwTranslate(Base, &Physical) || Physical != Base)
        return FALSE;

    Control = (volatile ULONG*)(ULONG_PTR)(Base + OFW_BOCHS_ENDIAN_REGISTER);
    *Control = OFW_BOCHS_LITTLE_ENDIAN;
    __asm__ __volatile__("eieio" ::: "memory");
    return *Control == OFW_BOCHS_LITTLE_ENDIAN;
}

BOOLEAN
OfwGetDisplay(
    _Out_ PULONG Address,
    _Out_ PULONG Physical,
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Stride)
{
    static BOOLEAN Probed, Present;
    static ULONG DisplayAddress, DisplayPhysical, DisplayWidth, DisplayHeight, DisplayStride;
    CHAR Path[OFW_DISPLAY_PATH];
    OFW_HANDLE Display;
    ULONG Depth;

    if (!Probed && OfwAvailable)
    {
        Probed = TRUE;
        Display = OfwFindDisplay(OfwFindDevice("/"));
        Present = Display != OFW_INVALID &&
                  OfwPackageToPath(Display, Path, sizeof(Path)) > 0 &&
                  OfwOpen(Path) != OFW_INVALID &&
                  OfwGetPropCell(Display, "address", &DisplayAddress) &&
                  OfwGetPropCell(Display, "width", &DisplayWidth) &&
                  OfwGetPropCell(Display, "height", &DisplayHeight) &&
                  OfwGetPropCell(Display, "depth", &Depth) &&
                  OfwGetPropCell(Display, "linebytes", &DisplayStride) &&
                  Depth == OFW_DISPLAY_DEPTH &&
                  DisplayWidth != 0 && DisplayHeight != 0 &&
                  DisplayStride >= DisplayWidth * sizeof(ULONG) &&
                  DisplayStride % sizeof(ULONG) == 0 &&
                  DisplayHeight <= MAXULONG / DisplayStride &&
                  OfwSetDisplayByteOrder(Display) &&
                  OfwTranslate(DisplayAddress, &DisplayPhysical) &&
                  DisplayPhysical != 0 &&
                  DisplayPhysical % OFW_PAGE_SIZE == 0;
    }

    if (!Present)
        return FALSE;

    *Address = DisplayAddress;
    *Physical = DisplayPhysical;
    *Width = DisplayWidth;
    *Height = DisplayHeight;
    *Stride = DisplayStride;
    return TRUE;
}

BOOLEAN
FwGetFrameBuffer(
    _Out_ PCM_FRAMEBUF_DEVICE_DATA Data,
    _Out_ PULONGLONG Base,
    _Out_ PULONG Size)
{
    ULONG Address, Physical, Width, Height, Stride;

    if (!OfwGetDisplay(&Address, &Physical, &Width, &Height, &Stride))
        return FALSE;

    RtlZeroMemory(Data, sizeof(*Data));
    Data->ScreenWidth = Width;
    Data->ScreenHeight = Height;
    Data->PixelsPerScanLine = Stride / sizeof(ULONG);
    Data->BitsPerPixel = OFW_DISPLAY_DEPTH;
    Data->PixelMasks.RedMask = OFW_MASK_RED;
    Data->PixelMasks.GreenMask = OFW_MASK_GREEN;
    Data->PixelMasks.BlueMask = OFW_MASK_BLUE;
    Data->PixelMasks.ReservedMask = OFW_MASK_RESERVED;
    Data->LogicalWidth = Width;
    Data->LogicalHeight = Height;

    *Base = Physical;
    *Size = Stride * Height;
    return TRUE;
}

BOOLEAN
FwGetEdid(
    _Out_writes_bytes_(Size) PUCHAR Edid,
    _In_ ULONG Size)
{
    UNREFERENCED_PARAMETER(Edid);
    UNREFERENCED_PARAMETER(Size);
    return FALSE;
}

static
UCHAR
OfwCmosRead(
    _In_ UCHAR Register)
{
    volatile UCHAR* Io = (volatile UCHAR*)(ULONG_PTR)OfwMachine.IsaIoPhysicalBase;

    Io[0x70] = Register;
    __asm__ __volatile__("eieio" ::: "memory");
    return Io[0x71];
}

static
USHORT
OfwBcd(
    _In_ UCHAR Value,
    _In_ BOOLEAN Binary)
{
    return Binary ? Value : (USHORT)((Value >> 4) * 10 + (Value & 0x0F));
}

BOOLEAN
FwGetTime(
    _Out_ PTIME_FIELDS Time)
{
    BOOLEAN Binary;

    if (OfwMachine.MachineType != PPC_MACHINE_PREP || !OfwAvailable)
        return FALSE;

    while (OfwCmosRead(0x0A) & 0x80)
        ;

    Binary = (OfwCmosRead(0x0B) & 0x04) != 0;
    RtlZeroMemory(Time, sizeof(*Time));
    Time->Second = OfwBcd(OfwCmosRead(0x00), Binary);
    Time->Minute = OfwBcd(OfwCmosRead(0x02), Binary);
    Time->Hour = OfwBcd(OfwCmosRead(0x04), Binary);
    Time->Day = OfwBcd(OfwCmosRead(0x07), Binary);
    Time->Month = OfwBcd(OfwCmosRead(0x08), Binary);
    Time->Year = OfwBcd(OfwCmosRead(0x09), Binary);
    Time->Year += (Time->Year < 70) ? 2000 : 1900;
    return TRUE;
}

PCWSTR
FwGetLoadOptions(
    _Out_ PULONG Chars)
{
    *Chars = OfwParameters->LoadOptions ? (ULONG)wcslen(OfwParameters->LoadOptions) + 1 : 0;
    return OfwParameters->LoadOptions;
}

BOOLEAN
FwGetLoaderImage(
    _Out_ PVOID* Base,
    _Out_ PSIZE_T Size)
{
    *Base = OfwParameters->ImageBase;
    *Size = OfwParameters->ImageSize;
    return *Base != NULL;
}

BOOLEAN
FwGetImageDirectory(
    _Out_writes_z_(Chars) PWSTR Directory,
    _In_ ULONG Chars)
{
    ULONG i, End = 0;

    for (i = 0; OfwParameters->ImagePath[i] != UNICODE_NULL && i < Chars - 1; i++)
    {
        Directory[i] = OfwParameters->ImagePath[i];
        if (Directory[i] == L'\\')
            End = i + 1;
    }

    if (Chars)
        Directory[End] = UNICODE_NULL;

    return End != 0;
}

static
ULONGLONG
OfwReadTimebase(VOID)
{
    ULONG Upper, Lower, Check;

    do
    {
        __asm__ __volatile__("mftbu %0" : "=r"(Upper));
        __asm__ __volatile__("mftb %0" : "=r"(Lower));
        __asm__ __volatile__("mftbu %0" : "=r"(Check));
    } while (Upper != Check);

    return ((ULONGLONG)Upper << 32) | Lower;
}

VOID
StallExecutionProcessor(
    ULONG Microseconds)
{
    ULONGLONG Start = OfwReadTimebase();
    ULONGLONG Ticks = (ULONGLONG)Microseconds * OfwMachine.TimebaseFrequency / 1000000;

    while (OfwReadTimebase() - Start < Ticks)
        ;
}

VOID
PpcFlushCacheRange(
    _In_ PVOID Address,
    _In_ SIZE_T Length)
{
    ULONG_PTR Line = OfwMachine.DcacheLineSize ? OfwMachine.DcacheLineSize : 32;
    ULONG_PTR Start = (ULONG_PTR)Address & ~(Line - 1);
    ULONG_PTR End = (ULONG_PTR)Address + Length;

    for (; Start < End; Start += Line)
        __asm__ __volatile__("dcbst 0, %0" :: "r"(Start) : "memory");
    __asm__ __volatile__("sync" ::: "memory");

    for (Start = (ULONG_PTR)Address & ~(Line - 1); Start < End; Start += Line)
        __asm__ __volatile__("icbi 0, %0" :: "r"(Start) : "memory");
    __asm__ __volatile__("sync; isync" ::: "memory");
}

VOID
OfwDetectMachine(VOID)
{
    OFW_HANDLE Root, Cpus, Cpu;
    CHAR Buffer[64];
    LONG Length;
    ULONG Value;

    RtlZeroMemory(&OfwMachine, sizeof(OfwMachine));
    OfwMachine.MachineType = PPC_MACHINE_UNKNOWN;
    OfwMachine.DcacheLineSize = 32;
    OfwMachine.IcacheLineSize = 32;

    Root = OfwFindDevice("/");
    Length = OfwGetProp(Root, "model", OfwMachine.Model, sizeof(OfwMachine.Model) - 1);
    OfwMachine.Model[max(Length, 0)] = ANSI_NULL;

    if (OfwFindDevice("/pci@80000000") != OFW_INVALID)
    {
        OfwMachine.MachineType = PPC_MACHINE_PREP;
        OfwMachine.IsaIoPhysicalBase = PREP_ISA_IO_BASE;
        OfwMachine.PciMemoryPhysicalBase = PREP_PCI_MEMORY_BASE;
        OfwMachine.PciDmaOffset = PREP_PCI_DMA_OFFSET;
        OfwMachine.ConsolePort = PREP_COM1_PORT;
        if (OfwMachine.Model[0] == ANSI_NULL)
            RtlStringCbCopyA(OfwMachine.Model, sizeof(OfwMachine.Model), "PReP");
    }

    Cpus = OfwFindDevice("/cpus");
    for (Cpu = (Cpus != OFW_INVALID) ? OfwChild(Cpus) : OFW_INVALID; Cpu != OFW_INVALID; Cpu = OfwPeer(Cpu))
    {
        Length = OfwGetProp(Cpu, "device_type", Buffer, sizeof(Buffer) - 1);
        if (Length < 3 || !RtlEqualMemory(Buffer, "cpu", 3))
            continue;

        if (OfwGetPropCell(Cpu, "timebase-frequency", &Value))
            OfwMachine.TimebaseFrequency = Value;
        if (OfwGetPropCell(Cpu, "clock-frequency", &Value))
            OfwMachine.ProcessorFrequency = Value;
        if (OfwGetPropCell(Cpu, "bus-frequency", &Value))
            OfwMachine.BusFrequency = Value;
        if (OfwGetPropCell(Cpu, "d-cache-block-size", &Value) && Value != 0)
            OfwMachine.DcacheLineSize = Value;
        if (OfwGetPropCell(Cpu, "i-cache-block-size", &Value) && Value != 0)
            OfwMachine.IcacheLineSize = Value;
        if (OfwGetPropCell(Cpu, "d-cache-size", &Value))
            OfwMachine.DcacheSize = Value;
        if (OfwGetPropCell(Cpu, "i-cache-size", &Value))
            OfwMachine.IcacheSize = Value;
        break;
    }

    if (OfwMachine.TimebaseFrequency == 0)
        OfwMachine.TimebaseFrequency = PREP_TIMEBASE;
}

BOOLEAN
FwStartImage(
    _In_reads_bytes_(Size) PVOID Image,
    _In_ ULONG Size,
    _In_ PCWSTR LoadOptions)
{
    OFW_APPLICATION_PARAMETERS Parameters;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER Section;
    POFW_APPLICATION_ENTRY Entry;
    PUCHAR Base;
    ULONG ImageSize, Copy, i;

    if (!NT_SUCCESS(RtlImageNtHeaderEx(0, Image, Size, &NtHeaders)))
        return FALSE;

    ImageSize = ALIGN_UP_BY(NtHeaders->OptionalHeader.SizeOfImage, OFW_PAGE_SIZE);
    Base = (PUCHAR)(ULONG_PTR)NtHeaders->OptionalHeader.ImageBase;
    if (!FwAllocatePagesAt(Base, ImageSize))
    {
        Base = FwAllocatePages(ImageSize);
        if (!Base)
            return FALSE;
    }

    RtlZeroMemory(Base, ImageSize);
    RtlCopyMemory(Base, Image, min(NtHeaders->OptionalHeader.SizeOfHeaders, Size));

    Section = IMAGE_FIRST_SECTION(NtHeaders);
    for (i = 0; i < NtHeaders->FileHeader.NumberOfSections; i++, Section++)
    {
        Copy = Section->SizeOfRawData;
        if (Section->Misc.VirtualSize != 0 && Copy > Section->Misc.VirtualSize)
            Copy = Section->Misc.VirtualSize;

        if (Section->PointerToRawData + Copy > Size || Section->VirtualAddress + Copy > ImageSize)
        {
            FwFreePages(Base, ImageSize);
            return FALSE;
        }

        RtlCopyMemory(Base + Section->VirtualAddress, (PUCHAR)Image + Section->PointerToRawData, Copy);
    }

    if ((ULONG_PTR)Base != NtHeaders->OptionalHeader.ImageBase &&
        !NT_SUCCESS(LdrRelocateImageWithBias(Base, 0, "", STATUS_SUCCESS,
                                             STATUS_CONFLICTING_ADDRESSES, STATUS_INVALID_IMAGE_FORMAT)))
    {
        FwFreePages(Base, ImageSize);
        return FALSE;
    }

    PpcFlushCacheRange(Base, ImageSize);

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.Magic = OFW_APPLICATION_MAGIC;
    Parameters.Version = OFW_APPLICATION_VERSION;
    Parameters.Stage0 = OfwStage0Info;
    Parameters.Shared = OfwShared;
    Parameters.ImageBase = Base;
    Parameters.ImageSize = ImageSize;
    Parameters.BootDisk = OfwParameters->BootDisk;
    Parameters.BootPartition = OfwParameters->BootPartition;
    Parameters.LoadOptions = LoadOptions;

    Entry = (POFW_APPLICATION_ENTRY)(Base + NtHeaders->OptionalHeader.AddressOfEntryPoint);
    Entry(&Parameters);

    FwFreePages(Base, ImageSize);
    return FALSE;
}

DECLSPEC_NORETURN
VOID
FwHalt(VOID)
{
    if (OfwAvailable)
        OfwExit();

    for (;;)
        ;
}

static
BOOLEAN
OfwInitializeRoot(
    _In_ PPPC_STAGE0_INFO Info)
{
    ULONG Size = ALIGN_UP_BY(sizeof(OFW_SHARED_STATE), OFW_PAGE_SIZE);
    ULONG Base;

    if (!OfwInitialize(Info))
        return FALSE;

    Base = OfwClaim(0, Size, OFW_PAGE_SIZE);
    if (Base == (ULONG)-1 || Base == 0)
        return FALSE;

    OfwShared = (POFW_SHARED_STATE)(ULONG_PTR)Base;
    RtlZeroMemory(OfwShared, Size);
    OfwRecordAllocation(Base, Size);

    OfwParameters = &OfwRootParameters;
    OfwParameters->Magic = OFW_APPLICATION_MAGIC;
    OfwParameters->Version = OFW_APPLICATION_VERSION;
    OfwParameters->Stage0 = Info;
    OfwParameters->Shared = OfwShared;
    OfwParameters->ImageBase = (PVOID)(ULONG_PTR)Info->ImageBase;
    OfwParameters->ImageSize = Info->ImageSize;
    return TRUE;
}

VOID
OfwEntry(
    _In_ PVOID Argument)
{
    POFW_APPLICATION_PARAMETERS Parameters = Argument;

    if (Parameters && Parameters->Magic == OFW_APPLICATION_MAGIC)
    {
        if (Parameters->Version != OFW_APPLICATION_VERSION || !OfwInitialize(Parameters->Stage0))
            return;

        OfwParameters = Parameters;
        OfwShared = Parameters->Shared;
        OfwDetectMachine();
        BlMain();
        return;
    }

    if (!OfwInitializeRoot(Argument))
    {
        for (;;)
            ;
    }

    OfwDetectMachine();
    OfwFindBootDevice();
    BlMain();
    FwHalt();
}
