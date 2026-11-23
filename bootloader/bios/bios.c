/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PC/AT BIOS entry point, BIOS calls, timer and system tables
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <bios.h>

#define BIOS_PRINT_CHARS        512
#define BIOS_PIT_FREQUENCY      1193182
#define BIOS_PIT_LATCH          11932
#define BIOS_PIT_SPINS          100000000
#define BIOS_DEFAULT_TSC        1000000000ULL
#define BIOS_BDA_EBDA_SEGMENT   0x40E
#define BIOS_EBDA_SCAN          0x400
#define BIOS_TABLE_SCAN_BASE    0xE0000
#define BIOS_SMBIOS_SCAN_BASE   0xF0000
#define BIOS_TABLE_SCAN_END     0x100000
#define BIOS_PARAGRAPH          16
#define BIOS_RSDP_V1_LENGTH     20
#define BIOS_RSDP_LENGTH_OFFSET 20
#define BIOS_RSDP_V2_MINIMUM    36
#define BIOS_SM3_LENGTH_OFFSET  6
#define BIOS_SM_LENGTH_OFFSET   5
#define BIOS_SM_DMI_OFFSET      16
#define BIOS_SM_DMI_LENGTH      15
#define BIOS_CMOS_INDEX         0x70
#define BIOS_CMOS_DATA          0x71
#define BIOS_CMOS_SPINS         100000
#define BIOS_ROOT_DIRECTORY     L"\\Boot\\"

typedef VOID (__cdecl *PBIOS_APPLICATION_ENTRY)(PVOID Argument);

const USHORT FwApplicationSubsystem = IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION;
const USHORT FwApplicationMachine = IMAGE_FILE_MACHINE_NATIVE;
const BOOLEAN FwUefiFirmware = FALSE;
VOID (*FwLateDebugPutChar)(UCHAR Character);

PBIOS_STARTUP_INFO BiosStartup;
PBIOS_SHARED_STATE BiosShared;
PBIOS_APPLICATION_PARAMETERS BiosParameters;

static BIOS_SHARED_STATE BiosRootShared;
static BIOS_APPLICATION_PARAMETERS BiosRootParameters;

VOID
BiosCall(
    _In_ UCHAR Interrupt,
    _Inout_ PBIOS_REGISTERS Registers)
{
    PBIOS_CALL Gate;

    ASSERT(BiosStartup != NULL);
    ASSERT(BiosStartup->Call >= BIOS_STUB_BASE && BiosStartup->Call < BIOS_STUB_BASE + BIOS_STUB_SIZE);

    Gate = (PBIOS_CALL)(ULONG_PTR)BiosStartup->Call;
    Gate(Interrupt, Registers);
}

USHORT
BiosSegment(
    _In_ const VOID* Address)
{
    ASSERT((ULONG_PTR)Address < BIOS_CONVENTIONAL_LIMIT);
    return (USHORT)((ULONG_PTR)Address >> 4);
}

USHORT
BiosOffset(
    _In_ const VOID* Address)
{
    ASSERT((ULONG_PTR)Address < BIOS_CONVENTIONAL_LIMIT);
    return (USHORT)((ULONG_PTR)Address & 0xF);
}

PVOID
BiosLinear(
    _In_ ULONG FarPointer)
{
    return (PVOID)(ULONG_PTR)(((FarPointer >> 16) << 4) + (FarPointer & 0xFFFF));
}

ULONGLONG
BiosReadTsc(VOID)
{
    return __rdtsc();
}

VOID
BiosCalibrateTimer(VOID)
{
    ULONGLONG Start, End;
    UCHAR Gate;
    ULONG Spins;

    if (BiosShared->TscFrequency != 0)
        return;

    Gate = __inbyte(0x61);
    __outbyte(0x61, (UCHAR)((Gate & ~0x02) | 0x01));
    __outbyte(0x43, 0xB0);
    __outbyte(0x42, (UCHAR)(BIOS_PIT_LATCH & 0xFF));
    __outbyte(0x42, (UCHAR)(BIOS_PIT_LATCH >> 8));

    Start = __rdtsc();
    for (Spins = 0; Spins < BIOS_PIT_SPINS && !(__inbyte(0x61) & 0x20); Spins++)
        ;
    End = __rdtsc();
    __outbyte(0x61, Gate);

    if (Spins == BIOS_PIT_SPINS || End <= Start)
    {
        /* FIXME: no PIT channel 2 answered; the stall loops assume a 1 GHz time stamp counter */
        BiosShared->TscFrequency = BIOS_DEFAULT_TSC;
        return;
    }

    BiosShared->TscFrequency = (End - Start) * BIOS_PIT_FREQUENCY / BIOS_PIT_LATCH;
    ASSERT(BiosShared->TscFrequency != 0);
}

ULONG
BiosMilliseconds(VOID)
{
    ASSERT(BiosShared->TscFrequency >= 1000);
    return (ULONG)(__rdtsc() / (BiosShared->TscFrequency / 1000));
}

VOID
StallExecutionProcessor(
    ULONG Microseconds)
{
    ULONGLONG Start = __rdtsc();
    ULONGLONG Ticks;

    ASSERT(BiosShared != NULL && BiosShared->TscFrequency != 0);
    Ticks = (ULONGLONG)Microseconds * BiosShared->TscFrequency / 1000000;

    while (__rdtsc() - Start < Ticks)
        YieldProcessor();
}

VOID
FwPrint(
    _In_ PCWSTR Format,
    ...)
{
    WCHAR Text[BIOS_PRINT_CHARS];
    CHAR Narrow[BIOS_PRINT_CHARS];
    va_list Arguments;
    ULONG i;

    va_start(Arguments, Format);
    RtlStringCchVPrintfW(Text, RTL_NUMBER_OF(Text), Format, Arguments);
    va_end(Arguments);

    for (i = 0; Text[i] != UNICODE_NULL; i++)
        Narrow[i] = Text[i] <= 0x7F ? (CHAR)Text[i] : '?';
    Narrow[i] = ANSI_NULL;

    BiosConsoleWrite(Narrow, i);
    if (FwDebugEnabled && !FwDebugScreen)
    {
        for (i = 0; Narrow[i] != ANSI_NULL; i++)
            FwArchDebugPutChar((UCHAR)Narrow[i]);
    }
}

VOID
FwDebugEnable(VOID)
{
    FwDebugEnabled = TRUE;
}

VOID
FwDebugPrint(
    _In_ PCSTR Text)
{
    PCSTR Next;

    if (!FwDebugEnabled)
        return;

    for (Next = Text; *Next; Next++)
    {
        if (*Next == '\n')
            FwArchDebugPutChar('\r');
        FwArchDebugPutChar((UCHAR)*Next);
    }

    if (FwDebugScreen)
        BiosConsoleWrite(Text, (ULONG)strlen(Text));
}

VOID
FwDebugAnnounce(
    _In_ PCSTR Application,
    _In_ BOOLEAN NewLog)
{
    CHAR Line[128];

    UNREFERENCED_PARAMETER(NewLog);

    if (!FwDebugEnabled)
        return;

    RtlStringCbPrintfA(Line,
                       sizeof(Line),
                       "\n%s, PC/AT BIOS, boot drive 0x%02lX, medium %lu, volume at sector %I64u\n",
                       Application,
                       BiosStartup->BootDrive,
                       BiosStartup->BootMedium,
                       BiosStartup->PartitionStart);
    FwDebugPrint(Line);
}

DECLSPEC_NORETURN
VOID
FwHalt(VOID)
{
    _disable();
    for (;;)
        __halt();
}

static
BOOLEAN
BiosChecksum(
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ ULONG Length)
{
    UCHAR Sum = 0;
    ULONG i;

    for (i = 0; i < Length; i++)
        Sum = (UCHAR)(Sum + Data[i]);

    return Sum == 0;
}

static
PVOID
BiosScanRsdp(
    _In_ ULONG_PTR Base,
    _In_ ULONG_PTR End)
{
    const UCHAR* Candidate;
    ULONG Length;

    for (; Base + BIOS_RSDP_V1_LENGTH <= End; Base += BIOS_PARAGRAPH)
    {
        Candidate = (const UCHAR*)Base;
        if (!RtlEqualMemory(Candidate, "RSD PTR ", 8) || !BiosChecksum(Candidate, BIOS_RSDP_V1_LENGTH))
            continue;

        if (Candidate[15] >= 2)
        {
            Length = LeUlong(Candidate + BIOS_RSDP_LENGTH_OFFSET);
            if (Length < BIOS_RSDP_V2_MINIMUM || Base + Length > End || !BiosChecksum(Candidate, Length))
                continue;
        }

        return (PVOID)Base;
    }

    return NULL;
}

PVOID
FwGetAcpiRootPointer(VOID)
{
    ULONG_PTR Ebda = (ULONG_PTR)*(volatile USHORT*)BIOS_BDA_EBDA_SEGMENT << 4;
    PVOID Rsdp = NULL;

    if (Ebda >= 0x80000 && Ebda < BIOS_CONVENTIONAL_LIMIT)
        Rsdp = BiosScanRsdp(Ebda, Ebda + BIOS_EBDA_SCAN);

    return Rsdp ? Rsdp : BiosScanRsdp(BIOS_TABLE_SCAN_BASE, BIOS_TABLE_SCAN_END);
}

PVOID
FwGetSmbiosEntryPoint(VOID)
{
    const UCHAR* Candidate;
    PVOID Legacy = NULL;
    ULONG_PTR Base;

    for (Base = BIOS_SMBIOS_SCAN_BASE; Base + BIOS_PARAGRAPH * 2 <= BIOS_TABLE_SCAN_END; Base += BIOS_PARAGRAPH)
    {
        Candidate = (const UCHAR*)Base;
        if (RtlEqualMemory(Candidate, "_SM3_", 5) &&
            Candidate[BIOS_SM3_LENGTH_OFFSET] >= 0x18 &&
            BiosChecksum(Candidate, Candidate[BIOS_SM3_LENGTH_OFFSET]))
        {
            return (PVOID)Base;
        }

        if (!Legacy &&
            RtlEqualMemory(Candidate, "_SM_", 4) &&
            Candidate[BIOS_SM_LENGTH_OFFSET] >= 0x1F &&
            BiosChecksum(Candidate, Candidate[BIOS_SM_LENGTH_OFFSET]) &&
            RtlEqualMemory(Candidate + BIOS_SM_DMI_OFFSET, "_DMI_", 5) &&
            BiosChecksum(Candidate + BIOS_SM_DMI_OFFSET, BIOS_SM_DMI_LENGTH))
        {
            Legacy = (PVOID)Base;
        }
    }

    return Legacy;
}

BOOLEAN
FwGetFirmwareResource(
    _In_ ULONG Index,
    _Out_ struct _LOADER_FIRMWARE_RESOURCE* Resource)
{
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Resource);
    return FALSE;
}

static
UCHAR
BiosCmosRead(
    _In_ UCHAR Register)
{
    __outbyte(BIOS_CMOS_INDEX, Register);
    return __inbyte(BIOS_CMOS_DATA);
}

static
USHORT
BiosFromBcd(
    _In_ UCHAR Value,
    _In_ BOOLEAN Binary)
{
    return Binary ? Value : (USHORT)((Value >> 4) * 10 + (Value & 0x0F));
}

BOOLEAN
FwGetTime(
    _Out_ PTIME_FIELDS Time)
{
    UCHAR Status, Hour;
    BOOLEAN Binary, Pm;
    ULONG Spins;

    for (Spins = 0; Spins < BIOS_CMOS_SPINS && (BiosCmosRead(0x0A) & 0x80); Spins++)
        ;

    Status = BiosCmosRead(0x0B);
    Binary = (Status & 0x04) != 0;
    Hour = BiosCmosRead(0x04);
    Pm = !(Status & 0x02) && (Hour & 0x80);

    RtlZeroMemory(Time, sizeof(*Time));
    Time->Second = BiosFromBcd(BiosCmosRead(0x00), Binary);
    Time->Minute = BiosFromBcd(BiosCmosRead(0x02), Binary);
    Time->Hour = BiosFromBcd(Hour & 0x7F, Binary);
    Time->Day = BiosFromBcd(BiosCmosRead(0x07), Binary);
    Time->Month = BiosFromBcd(BiosCmosRead(0x08), Binary);
    Time->Year = BiosFromBcd(BiosCmosRead(0x09), Binary);
    Time->Year += (Time->Year < 70) ? 2000 : 1900;

    if (!(Status & 0x02))
    {
        if (Time->Hour == 12)
            Time->Hour = 0;
        if (Pm)
            Time->Hour += 12;
    }

    return Time->Month >= 1 && Time->Month <= 12 && Time->Day >= 1 && Time->Day <= 31;
}

PCWSTR
FwGetLoadOptions(
    _Out_ PULONG Chars)
{
    *Chars = BiosParameters->LoadOptions ? (ULONG)wcslen(BiosParameters->LoadOptions) + 1 : 0;
    return BiosParameters->LoadOptions;
}

BOOLEAN
FwGetLoaderImage(
    _Out_ PVOID* Base,
    _Out_ PSIZE_T Size)
{
    *Base = BiosParameters->ImageBase;
    *Size = BiosParameters->ImageSize;
    return *Base != NULL;
}

BOOLEAN
FwGetImageDirectory(
    _Out_writes_z_(Chars) PWSTR Directory,
    _In_ ULONG Chars)
{
    ULONG i, End = 0;

    if (Chars == 0)
        return FALSE;

    for (i = 0; BiosParameters->ImagePath[i] != UNICODE_NULL && i < Chars - 1; i++)
    {
        Directory[i] = BiosParameters->ImagePath[i];
        if (Directory[i] == L'\\')
            End = i + 1;
    }

    Directory[End] = UNICODE_NULL;
    return End != 0;
}

BOOLEAN
FwStartImage(
    _In_reads_bytes_(Size) PVOID Image,
    _In_ ULONG Size,
    _In_ PCWSTR LoadOptions,
    _Out_ PBOOLEAN Cancelled)
{
    BIOS_APPLICATION_PARAMETERS Parameters;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER Section;
    PBIOS_APPLICATION_ENTRY Entry;
    PUCHAR Base;
    ULONG ImageSize, Copy, i;

    *Cancelled = FALSE;

    if (!NT_SUCCESS(RtlImageNtHeaderEx(0, Image, Size, &NtHeaders)) ||
        NtHeaders->OptionalHeader.SizeOfImage == 0 ||
        NtHeaders->OptionalHeader.AddressOfEntryPoint >= NtHeaders->OptionalHeader.SizeOfImage)
    {
        return FALSE;
    }

    ImageSize = ALIGN_UP_BY(NtHeaders->OptionalHeader.SizeOfImage, PAGE_SIZE);
    Base = (PUCHAR)(ULONG_PTR)NtHeaders->OptionalHeader.ImageBase;
    if ((ULONGLONG)NtHeaders->OptionalHeader.ImageBase + ImageSize > BIOS_IDENTITY_LIMIT ||
        !FwAllocatePagesAt(Base, ImageSize))
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

        if (Section->PointerToRawData > Size ||
            Copy > Size - Section->PointerToRawData ||
            Section->VirtualAddress > ImageSize ||
            Copy > ImageSize - Section->VirtualAddress)
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

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.Magic = BIOS_APPLICATION_MAGIC;
    Parameters.Version = BIOS_APPLICATION_VERSION;
    Parameters.Startup = BiosStartup;
    Parameters.Shared = BiosShared;
    Parameters.ImageBase = Base;
    Parameters.ImageSize = ImageSize;
    Parameters.BootDisk = BiosParameters->BootDisk;
    Parameters.BootPartition = BiosParameters->BootPartition;
    Parameters.LoadOptions = LoadOptions;

    Entry = (PBIOS_APPLICATION_ENTRY)(Base + NtHeaders->OptionalHeader.AddressOfEntryPoint);
    Entry(&Parameters);

    FwFreePages(Base, ImageSize);
    return FALSE;
}

VOID
__cdecl
BiosEntry(
    _In_ PVOID Argument)
{
    PBIOS_APPLICATION_PARAMETERS Parameters = Argument;
    PBIOS_STARTUP_INFO Startup = Argument;

    if (Parameters->Magic == BIOS_APPLICATION_MAGIC)
    {
        if (Parameters->Version != BIOS_APPLICATION_VERSION)
            return;

        BiosParameters = Parameters;
        BiosStartup = Parameters->Startup;
        BiosShared = Parameters->Shared;
        BiosMemoryInitialize(FALSE);
        BiosDiskInitialize(FALSE);
        BiosConsoleInitialize(FALSE);
        BlMain();
        return;
    }

    if (Startup->Magic != BIOS_STARTUP_MAGIC || Startup->Version != BIOS_STARTUP_VERSION)
        FwHalt();

    BiosStartup = Startup;
    BiosShared = &BiosRootShared;
    BiosParameters = &BiosRootParameters;
    BiosParameters->Magic = BIOS_APPLICATION_MAGIC;
    BiosParameters->Version = BIOS_APPLICATION_VERSION;
    BiosParameters->Startup = Startup;
    BiosParameters->Shared = BiosShared;
    BiosParameters->ImageBase = (PVOID)(ULONG_PTR)Startup->ImageBase;
    BiosParameters->ImageSize = ALIGN_UP_BY(Startup->ImageSize, PAGE_SIZE);
    RtlStringCchCopyW(BiosParameters->ImagePath, RTL_NUMBER_OF(BiosParameters->ImagePath), BIOS_ROOT_DIRECTORY);

    BiosCalibrateTimer();
    BiosMemoryInitialize(TRUE);
    BiosDiskInitialize(TRUE);
    BiosConsoleInitialize(TRUE);
    BlMain();
    FwHalt();
}
