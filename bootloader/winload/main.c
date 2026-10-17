/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Operating system loader main routine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"

#define OSL_STACK_SIZE    0x40000
#define OSL_MAX_ENTRIES   16
#define OSL_KERNEL_MODULE "ntoskrnl.exe"
#define OSL_HAL_MODULE    "hal.dll"
#define OSL_VIDEO_MODULE  "bootvid.dll"
#define OSL_FRAMEBUFFER_VIDEO_FILE "lfbbvid.dll"
#define OSL_RAMDISK_SIZE_OPTION "RDRAMSIZE="
#define OSL_DOWNLOAD_URL_OPTION "HTTPBOOTURL="
#define OSL_DOWNLOAD_ADDRESS_OPTION "HTTPBOOTIP="
#define OSL_ADDRESS_CHARS 64
#define OSL_STARTUP_PREFIX_CHARS (RTL_NUMBER_OF(BM_STARTUP_OPTION) - 1)

PVOID BasicStack;
PVOID UefiServiceStack;

static const PCWSTR OslStorePaths[] =
{
    L"\\EFI\\LiberNT\\Boot\\BCD",
    L"\\EFI\\Microsoft\\Boot\\BCD",
    L"\\Boot\\BCD",
};

static BM_ENTRY OslEntries[OSL_MAX_ENTRIES];
static OSL_PARAMETERS OslParameters;
static BM_STARTUP OslStartup;

VOID
UiMessageBox(
    _In_ PCSTR Format,
    ...)
{
    CHAR Text[OSL_OPTIONS_CHARS];
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);

    FwPrint(L"%hs\r\n", Text);
}

static
BOOLEAN
OslNarrow(
    _Out_writes_z_(Chars) PSTR Buffer,
    _In_ ULONG Chars,
    _In_ PCWSTR Text)
{
    ULONG i;

    for (i = 0; Text[i] != UNICODE_NULL; i++)
    {
        if (i >= Chars - 1 || Text[i] > 0x7F)
            return FALSE;
        Buffer[i] = (CHAR)Text[i];
    }

    Buffer[i] = ANSI_NULL;
    return TRUE;
}

static
VOID
OslParseStartup(
    _In_reads_(Chars) PCWSTR Text,
    _In_ ULONG Chars)
{
    ULONG i;

    RtlZeroMemory(&OslStartup, sizeof(OslStartup));

    for (i = 0; i < Chars && Text[i] >= L'0' && Text[i] <= L'9'; i++)
        OslStartup.Profile = OslStartup.Profile * 10 + (Text[i] - L'0');
}

static
PBM_ENTRY
OslSelectEntry(VOID)
{
    WCHAR StorePath[OSL_PATH_CHARS] = L"";
    WCHAR Id[BCD_ID_CHARS] = L"";
    PCWSTR Options;
    PVOID Store = NULL;
    ULONG Chars, Size, Count, Default, Start, i;

    Options = FwGetLoadOptions(&Chars);
    for (i = 0; Options && i < Chars && Options[i] != UNICODE_NULL; i++)
    {
        if (i != 0 && Options[i - 1] != L' ')
            continue;

        for (Start = i; i < Chars && Options[i] != UNICODE_NULL && Options[i] != L' '; i++)
            ;

        if (Options[Start] == L'{')
            RtlStringCchCopyNW(Id, RTL_NUMBER_OF(Id), &Options[Start], i - Start);
        else if (Options[Start] == L'\\')
            RtlStringCchCopyNW(StorePath, RTL_NUMBER_OF(StorePath), &Options[Start], i - Start);
        else if (i - Start > OSL_STARTUP_PREFIX_CHARS &&
                 _wcsnicmp(&Options[Start], BM_STARTUP_OPTION, OSL_STARTUP_PREFIX_CHARS) == 0)
            OslParseStartup(&Options[Start + OSL_STARTUP_PREFIX_CHARS], i - Start - OSL_STARTUP_PREFIX_CHARS);
    }

    if (StorePath[0] != UNICODE_NULL)
        Store = FwReadBootFile(StorePath, &Size);

    for (i = 0; i < RTL_NUMBER_OF(OslStorePaths) && !Store; i++)
        Store = FwReadBootFile(OslStorePaths[i], &Size);

    if (!Store || !BcdOpenStore(Store, Size))
    {
        FwPrint(L"The Boot Configuration Data store is missing or corrupt.\r\n");
        return NULL;
    }

    if (Id[0] != UNICODE_NULL)
    {
        if (BcdOpenObject(Id, &OslEntries[0].Object) &&
            OslEntries[0].Object.Type == BCD_OBJECT_OSLOADER)
        {
            return &OslEntries[0];
        }

        FwPrint(L"The boot entry %s is not an operating system loader entry.\r\n", Id);
        return NULL;
    }

    Count = BmEnumerateEntries(OslEntries, RTL_NUMBER_OF(OslEntries), &Default);
    if (Count == 0)
    {
        FwPrint(L"The Boot Configuration Data store has no operating system entry.\r\n");
        return NULL;
    }

    return &OslEntries[Default];
}

static
ULONGLONG
OslGetRamdiskExtraBytes(
    _In_ PCSTR Options)
{
    PCSTR Text = strstr(Options, OSL_RAMDISK_SIZE_OPTION);
    ULONGLONG Size = 0;

    if (!Text)
        return 0;

    for (Text += sizeof(OSL_RAMDISK_SIZE_OPTION) - 1; *Text >= '0' && *Text <= '9'; Text++)
        Size = Size * 10 + (*Text - '0');

    if (*Text == 'G' || *Text == 'g')
        Size <<= 30;
    else if (*Text == 'M' || *Text == 'm')
        Size <<= 20;
    else if (*Text == 'K' || *Text == 'k')
        Size <<= 10;

    return Size;
}

static
BOOLEAN
OslGetOption(
    _In_ PCSTR Options,
    _In_ PCSTR Name,
    _Out_writes_z_(Size) PSTR Value,
    _In_ SIZE_T Size)
{
    PCSTR Text = strstr(Options, Name);
    SIZE_T i;

    if (!Text)
        return FALSE;

    Text += strlen(Name);
    for (i = 0; i < Size - 1 && Text[i] != ANSI_NULL && Text[i] != ' '; i++)
        Value[i] = Text[i];

    Value[i] = ANSI_NULL;
    return i != 0;
}

static
PLDR_DATA_TABLE_ENTRY
OslLoadCoreImage(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Directory,
    _In_ PCWSTR FileName,
    _In_ PCSTR ModuleName,
    _In_ TYPE_OF_MEMORY MemoryType)
{
    CHAR Path[OSL_PATH_CHARS];
    PLDR_DATA_TABLE_ENTRY Entry;
    PVOID Base;

    if (!NT_SUCCESS(RtlStringCbPrintfA(Path, sizeof(Path), "%s%ws", Directory, FileName)))
        return NULL;

    Base = PeLoadImage(Device, Path, MemoryType);
    if (!Base)
    {
        FwPrint(L"%hs could not be loaded.\r\n", Path);
        return NULL;
    }

    if (!PeLdrAllocateDataTableEntry(&LoaderBlock->LoadOrderListHead,
                                     ModuleName,
                                     ModuleName,
                                     PaToVa(Base),
                                     &Entry))
    {
        return NULL;
    }

    PeInitializeSecurityCookie(Entry);
    return Entry;
}

static
VOID
OslMain(VOID)
{
    CHAR SystemRoot[OSL_PATH_CHARS], Directory[OSL_PATH_CHARS], Options[OSL_OPTIONS_CHARS];
    CHAR Url[OSL_PATH_CHARS], Address[OSL_ADDRESS_CHARS];
    PVOID Image = NULL;
    ULONG ImageSize = 0;
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    PLDR_DATA_TABLE_ENTRY Kernel, Hal, Video;
    PBM_ENTRY Entry;
    DK_DEVICE OsDevice;
    SIZE_T Length;

    Entry = OslSelectEntry();
    if (!Entry)
        return;

    if (!OslGetParameters(Entry, &OslParameters) ||
        !OslNarrow(SystemRoot, sizeof(SystemRoot) - 1, OslParameters.SystemRoot) ||
        !OslNarrow(Options, sizeof(Options), OslParameters.LoadOptions))
    {
        FwPrint(L"The boot entry has no usable OS device, system root or load options.\r\n");
        return;
    }

    AppendBootTimeOptions(Options, sizeof(Options), &OslStartup);

    Length = strlen(SystemRoot);
    if (Length == 0 || SystemRoot[Length - 1] != '\\')
        RtlStringCbCatA(SystemRoot, sizeof(SystemRoot), "\\");

    if (OslParameters.OsDevice.Ramdisk)
    {
        if (OslGetOption(Options, OSL_DOWNLOAD_URL_OPTION, Url, sizeof(Url)))
        {
            Image = FwDownloadFile(Url,
                                   OslGetOption(Options, OSL_DOWNLOAD_ADDRESS_OPTION, Address, sizeof(Address))
                                       ? Address : NULL,
                                   &ImageSize);
        }

        if (!OslBuildRamdisk(&OslParameters.OsDevice,
                             Image,
                             ImageSize,
                             OslGetRamdiskExtraBytes(Options),
                             &OsDevice))
        {
            return;
        }
    }
    else if (!DkOpenBcdDevice(&OslParameters.OsDevice, &OsDevice))
    {
        FwPrint(L"The operating system device was not found.\r\n");
        return;
    }

    PeSetSystemRoot(SystemRoot);
    RtlStringCbPrintfA(Directory, sizeof(Directory), "%ssystem32\\", SystemRoot);

    LoaderBlock = OslAllocateLoaderBlock();
    if (!LoaderBlock)
    {
        FwPrint(L"Not enough memory for the loader block.\r\n");
        return;
    }

    LoaderBlock->ConfigurationRoot = OslBuildConfigurationTree(LoaderBlock);

    if (!OslLoadSystemHive(LoaderBlock, &OsDevice, SystemRoot))
    {
        FwPrint(L"The SYSTEM registry hive is missing or corrupt.\r\n");
        return;
    }

    Kernel = OslLoadCoreImage(LoaderBlock,
                              &OsDevice,
                              Directory,
                              OslParameters.KernelFile,
                              OSL_KERNEL_MODULE,
                              LoaderSystemCode);
    Hal = Kernel ? OslLoadCoreImage(LoaderBlock,
                                    &OsDevice,
                                    Directory,
                                    OslParameters.HalFile,
                                    OSL_HAL_MODULE,
                                    LoaderHalCode)
                 : NULL;
    if (!Hal ||
        (FrameBufferData &&
         !PeLoadModule(&LoaderBlock->LoadOrderListHead,
                       &OsDevice,
                       Directory,
                       OSL_FRAMEBUFFER_VIDEO_FILE,
                       OSL_VIDEO_MODULE,
                       LoaderBootDriver,
                       LDRP_DRIVER_DEPENDENT_DLL,
                       &Video)) ||
        !PeResolveImports(&LoaderBlock->LoadOrderListHead, &OsDevice, Directory, Kernel) ||
        !PeResolveImports(&LoaderBlock->LoadOrderListHead, &OsDevice, Directory, Hal))
    {
        FwPrint(L"The kernel and the hardware abstraction layer could not be loaded.\r\n");
        return;
    }

    if (!OslLoadNlsData(LoaderBlock, &OsDevice, SystemRoot))
    {
        FwPrint(L"The national language support files could not be loaded.\r\n");
        return;
    }

    if (!OslLoadBootDrivers(LoaderBlock, &OsDevice, SystemRoot))
    {
        FwPrint(L"The boot driver list could not be built.\r\n");
        return;
    }

    OslLoadDriverDatabase(LoaderBlock, &OsDevice, SystemRoot);

    if (!OslFinalizeLoaderBlock(LoaderBlock, &OsDevice, SystemRoot, Options))
    {
        FwPrint(L"The operating system partition has no partition number.\r\n");
        return;
    }

    if (!OslArchPrepareHandoff(LoaderBlock))
    {
        FwPrint(L"The kernel address space could not be prepared.\r\n");
        return;
    }

    OslArchStartKernel(Kernel->EntryPoint, PaToVa(LoaderBlock));
}

VOID
ExecuteLoaderCleanly(
    PVOID PreviousStack)
{
    UefiServiceStack = PreviousStack;
    OslMain();
}

VOID
BlMain(VOID)
{
    PUCHAR Stack;

    OslArchInitialize();

    if (!MmInitializeMemoryManager())
    {
        FwPrint(L"The memory map could not be read.\r\n");
        return;
    }

    BlHeapAllocator = MmHeapAllocate;

    Stack = MmAllocateMemoryWithType(OSL_STACK_SIZE, LoaderOsloaderStack);
    if (!Stack)
    {
        FwPrint(L"Not enough memory for the loader stack.\r\n");
        return;
    }

    BasicStack = Stack + OSL_STACK_SIZE;
    OslArchSwitchStack();
}
