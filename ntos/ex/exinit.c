/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the executive up, in the order its parts need
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <ntos/inbv/logo.h>
#include <reactos/smpdbg.h>
#define NDEBUG
#include <debug.h>
#include <reactos/buildno.h>

/* GLOBALS ********************************************************************/

ULONG ExpInitializationPhase = 0;

ULONG NtMajorVersion = VER_PRODUCTMAJORVERSION;
ULONG NtMinorVersion = VER_PRODUCTMINORVERSION;
ULONG NtBuildNumber = VER_PRODUCTBUILD;
CHAR NtBuildLab[] = KERNEL_VERSION_BUILD_STR ".libernt";

ULONG CmNtCSDVersion = 0;
ULONG CmNtCSDReleaseType = 0;
ULONG CmNtSpBuildNumber = VER_PRODUCTBUILD_QFE;

WCHAR CmVersionStringBuffer[32] = {0};
UNICODE_STRING CmVersionString = {0, sizeof(CmVersionStringBuffer),
                                  CmVersionStringBuffer};

WCHAR CmCSDVersionStringBuffer[64] = {0};
UNICODE_STRING CmCSDVersionString = {0, sizeof(CmCSDVersionStringBuffer),
                                     CmCSDVersionStringBuffer};

ULONG ExSuiteMask = 0;

ULONG NtGlobalFlag = 0;

WCHAR NtSystemRootBuffer[260] = {0};
UNICODE_STRING NtSystemRoot = {0, sizeof(NtSystemRootBuffer),
                               NtSystemRootBuffer};

BOOLEAN ExpRealTimeIsUniversal = FALSE;
BOOLEAN ExCmosClockIsSane = TRUE;

PVOID ExpNlsTableBase = NULL;
ULONG ExpNlsTableSize = 0;
NLSTABLEINFO ExpNlsTableInfo;
PVOID ExpNlsSectionPointer = NULL;
ULONG ExpAnsiCodePageDataOffset = 0;
ULONG ExpOemCodePageDataOffset = 0;
ULONG ExpUnicodeCaseTableDataOffset = 0;

BOOLEAN InitIsWinPEMode = FALSE;
ULONG InitWinPEModeType = 0;
ULONG InitSafeBootMode = 0;
BOOLEAN ExpInTextModeSetup = FALSE;
BOOLEAN IoRemoteBootClient = FALSE;

CHAR NtInitialUserProcessBuffer[128] = "\\SystemRoot\\System32\\smss.exe";
ULONG NtInitialUserProcessBufferLength = sizeof("\\SystemRoot\\System32\\smss.exe") - sizeof(CHAR);
ULONG NtInitialUserProcessBufferType = REG_SZ;

/* PRIVATE DATA ***************************************************************/

#define EXP_INIT_TAG 'tinI'

/* What the loader's safe-boot switch means, in the order the switch names them */
#define EXP_SAFEBOOT_MINIMAL  1
#define EXP_SAFEBOOT_NETWORK  2
#define EXP_SAFEBOOT_DSREPAIR 3

/*
 * The first thing of its own the executive does on each processor, and the
 * last: an architecture may have work to do once the HAL has had its turn, and
 * every architecture is asked whether it has, so no caller has to know.
 */
CODE_SEG("INIT")
VOID
NTAPI
ExArchPostHalInitSystemPhase0(VOID);

CODE_SEG("INIT")
VOID
NTAPI
ExArchPostHalInitSystemPhase1(VOID);

CODE_SEG("INIT")
VOID
NTAPI
ExBurnMemory(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG PagesToDestroy);

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeTime(VOID);

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeSystemInformation(VOID);

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Reads one of the loader's switches. The switches are one string, so a switch
 * is only matched where a word begins.
 */
CODE_SEG("INIT")
static
PCSTR
ExpFindLoadOption(
    _In_opt_ PCSTR Options,
    _In_ PCSTR Name)
{
    SIZE_T Length = strlen(Name);
    PCSTR Current;

    if (Options == NULL)
    {
        return NULL;
    }

    for (Current = Options; *Current != ANSI_NULL; Current++)
    {
        if ((Current != Options) && (Current[-1] != ' ') &&
            (Current[-1] != '/'))
        {
            continue;
        }

        if (_strnicmp(Current, Name, Length) == 0)
        {
            return Current + Length;
        }
    }

    return NULL;
}

/* Only "Terminal Server" is a documented product suite name, and a workstation runs it for a single user */
CODE_SEG("INIT")
static
ULONG
ExpReadSuiteMask(VOID)
{
    static const WCHAR Terminal[] = L"Terminal Server";
    PCWSTR Name = CmSuiteBuffer;
    PCWSTR End;
    ULONG SuiteMask = 0;
    SIZE_T Length;

    if (CmSuiteBufferType == REG_MULTI_SZ)
    {
        End = CmSuiteBuffer + min(CmSuiteBufferLength, sizeof(CmSuiteBuffer)) / sizeof(WCHAR);
        while (Name < End && *Name != UNICODE_NULL)
        {
            Length = wcsnlen(Name, End - Name);
            if (Length == RTL_NUMBER_OF(Terminal) - 1 && _wcsnicmp(Name, Terminal, Length) == 0)
                SuiteMask |= VER_SUITE_TERMINAL;
            Name += Length + 1;
        }
    }

    if ((SuiteMask & VER_SUITE_TERMINAL) && CmTsAppCompat != 1)
        SuiteMask |= VER_SUITE_SINGLEUSERTS;

    return SuiteMask;
}

CODE_SEG("INIT")
static
VOID
ExpReadLoadOptions(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PCSTR Options = LoaderBlock->LoadOptions;
    PCSTR Value;

    if (ExpFindLoadOption(Options, "MININT") != NULL)
    {
        InitIsWinPEMode = TRUE;
        InitWinPEModeType = 1;
    }

    if (ExpFindLoadOption(Options, "EXECUTE") != NULL)
    {
        /* TODO: the switch that turns off no-execute enforcement is read but
           not acted on, because that is the memory manager's to honour. */
        DPRINT1("EXECUTE requested\n");
    }

    if (ExpFindLoadOption(Options, "SAFEBOOT") != NULL)
    {
        Value = ExpFindLoadOption(Options, "SAFEBOOT:MINIMAL");
        if (Value != NULL)
        {
            InitSafeBootMode = EXP_SAFEBOOT_MINIMAL;
        }

        Value = ExpFindLoadOption(Options, "SAFEBOOT:NETWORK");
        if (Value != NULL)
        {
            InitSafeBootMode = EXP_SAFEBOOT_NETWORK;
        }

        Value = ExpFindLoadOption(Options, "SAFEBOOT:DSREPAIR");
        if (Value != NULL)
        {
            InitSafeBootMode = EXP_SAFEBOOT_DSREPAIR;
        }
    }

    if (ExpFindLoadOption(Options, "NOGUIBOOT") != NULL)
    {
        DPRINT("Boot without a graphical screen requested\n");
    }

    Value = ExpFindLoadOption(Options, "BURNMEMORY=");
    if (Value != NULL)
    {
        ExBurnMemory(LoaderBlock, atol(Value));
    }

    if (ExpFindLoadOption(Options, "REMOTEBOOTCLIENT") != NULL)
    {
        IoRemoteBootClient = TRUE;
    }
}

/*
 * The loader and the kernel have to agree on the shape of what was handed over.
 * A block from a loader that is too old does not say what this kernel reads, so
 * nothing further can be trusted.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpIsLoaderValid(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_EXTENSION Extension = LoaderBlock->Extension;

    if (Extension == NULL)
    {
        return FALSE;
    }

    if (Extension->Size < sizeof(LOADER_PARAMETER_EXTENSION))
    {
        return FALSE;
    }

    if ((LoaderBlock->OsMajorVersion < VER_PRODUCTMAJORVERSION) ||
        ((LoaderBlock->OsMajorVersion == VER_PRODUCTMAJORVERSION) &&
         (LoaderBlock->OsMinorVersion < VER_PRODUCTMINORVERSION)))
    {
        return FALSE;
    }

    return TRUE;
}

/*
 * Takes pages away from what the memory manager will be given, so a machine can
 * be made to behave as though it had less memory than it has.
 */
CODE_SEG("INIT")
VOID
NTAPI
ExBurnMemory(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG PagesToDestroy)
{
    PLIST_ENTRY Entry;
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;

    DPRINT1("Burning %lu pages\n", PagesToDestroy);

    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        Descriptor = CONTAINING_RECORD(Entry,
                                       MEMORY_ALLOCATION_DESCRIPTOR,
                                       ListEntry);

        if ((Descriptor->MemoryType != LoaderFree) ||
            (Descriptor->PageCount < PagesToDestroy))
        {
            continue;
        }

        Descriptor->PageCount -= PagesToDestroy;
        Descriptor->BasePage += PagesToDestroy;
        return;
    }

    DPRINT1("No single free range holds %lu pages\n", PagesToDestroy);
}

/*
 * Names where the system was started from, so everything that follows can say
 * "\SystemRoot" instead of naming a device.
 */
CODE_SEG("INIT")
static
BOOLEAN
ExpAppendAnsi(
    _Inout_ PCHAR Buffer,
    _In_ SIZE_T BufferSize,
    _In_ PCSTR Text)
{
    SIZE_T Used = strlen(Buffer);
    SIZE_T Length = strlen(Text);

    if (Used + Length + 1 > BufferSize)
    {
        return FALSE;
    }

    RtlCopyMemory(&Buffer[Used], Text, Length + 1);

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpCreateSystemRootLink(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING LinkName = RTL_CONSTANT_STRING(L"\\SystemRoot");
    UNICODE_STRING TargetName;
    ANSI_STRING TargetPath;
    CHAR Buffer[512];
    HANDLE Handle;
    NTSTATUS Status;

    if ((LoaderBlock->ArcBootDeviceName == NULL) ||
        (LoaderBlock->NtBootPathName == NULL))
    {
        return FALSE;
    }

    Buffer[0] = ANSI_NULL;

    if (!ExpAppendAnsi(Buffer, sizeof(Buffer), "\\ArcName\\") ||
        !ExpAppendAnsi(Buffer, sizeof(Buffer), LoaderBlock->ArcBootDeviceName) ||
        !ExpAppendAnsi(Buffer, sizeof(Buffer), LoaderBlock->NtBootPathName))
    {
        return FALSE;
    }

    RtlInitAnsiString(&TargetPath, Buffer);

    /* The name of a link does not end with a separator, but the path does. */
    if (TargetPath.Length > sizeof(CHAR))
    {
        PCHAR Last = &TargetPath.Buffer[TargetPath.Length - 1];

        if (*Last == '\\')
        {
            *Last = ANSI_NULL;
            TargetPath.Length -= sizeof(CHAR);
        }
    }

    Status = RtlAnsiStringToUnicodeString(&TargetName, &TargetPath, TRUE);
    if (!NT_SUCCESS(Status))
    {
        return FALSE;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &LinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT |
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               SePublicDefaultUnrestrictedSd);

    Status = ZwCreateSymbolicLinkObject(&Handle,
                                        SYMBOLIC_LINK_ALL_ACCESS,
                                        &ObjectAttributes,
                                        &TargetName);

    RtlFreeUnicodeString(&TargetName);

    if (!NT_SUCCESS(Status))
    {
        return FALSE;
    }

    ZwClose(Handle);

    return TRUE;
}

/*
 * The machine whose images this system runs natively, read from the kernel's
 * own image so that it names whatever processor the kernel was built for.
 */
CODE_SEG("INIT")
static
VOID
ExpInitializeImageNumbers(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLDR_DATA_TABLE_ENTRY Kernel;
    PIMAGE_NT_HEADERS Headers;

    Kernel = CONTAINING_RECORD(LoaderBlock->LoadOrderListHead.Flink,
                               LDR_DATA_TABLE_ENTRY,
                               InLoadOrderLinks);

    Headers = RtlImageNtHeader(Kernel->DllBase);
    if (Headers == NULL) KeBugCheck(PHASE0_INITIALIZATION_FAILED);

    SharedUserData->ImageNumberLow = Headers->FileHeader.Machine;
    SharedUserData->ImageNumberHigh = Headers->FileHeader.Machine;
}

/*
 * The system directory as programs name it. Its volume has no letter until
 * the I/O manager gives it one, so the first letter stands in until then.
 */
CODE_SEG("INIT")
static
VOID
ExpInitializeSystemRoot(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ANSI_STRING BootPath;
    UNICODE_STRING Path;

    NtSystemRoot.Length = 0;
    RtlAppendUnicodeToString(&NtSystemRoot, L"C:");

    if (LoaderBlock->NtBootPathName != NULL)
    {
        RtlInitAnsiString(&BootPath, LoaderBlock->NtBootPathName);

        while (BootPath.Length != 0 && BootPath.Buffer[BootPath.Length - 1] == '\\')
        {
            BootPath.Length--;
        }

        Path.Buffer = &NtSystemRoot.Buffer[NtSystemRoot.Length / sizeof(WCHAR)];
        Path.Length = 0;
        Path.MaximumLength = NtSystemRoot.MaximumLength - NtSystemRoot.Length - sizeof(WCHAR);

        if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&Path, &BootPath, FALSE)))
        {
            NtSystemRoot.Length += Path.Length;
        }
    }

    RtlCopyMemory(SharedUserData->NtSystemRoot, NtSystemRoot.Buffer, NtSystemRoot.Length);
    SharedUserData->NtSystemRoot[NtSystemRoot.Length / sizeof(WCHAR)] = UNICODE_NULL;
}

/*
 * Puts the tables that say how bytes map to characters where everything can
 * reach them, because almost every name in the system is read through them.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitNls(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PNLS_DATA_BLOCK Nls = LoaderBlock->NlsData;
    PLIST_ENTRY Entry;
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;
    ULONG Size = 0;

    if ((Nls == NULL) || (Nls->AnsiCodePageData == NULL) ||
        (Nls->OemCodePageData == NULL) || (Nls->UnicodeCodePageData == NULL))
    {
        return FALSE;
    }

    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        Descriptor = CONTAINING_RECORD(Entry,
                                       MEMORY_ALLOCATION_DESCRIPTOR,
                                       ListEntry);

        if (Descriptor->MemoryType == LoaderNlsData)
        {
            Size = Descriptor->PageCount << PAGE_SHIFT;
            break;
        }
    }

    ExpNlsTableBase = Nls->AnsiCodePageData;
    ExpNlsTableSize = Size;

    ExpAnsiCodePageDataOffset = 0;
    ExpOemCodePageDataOffset =
        (ULONG)((ULONG_PTR)Nls->OemCodePageData -
                (ULONG_PTR)Nls->AnsiCodePageData);
    ExpUnicodeCaseTableDataOffset =
        (ULONG)((ULONG_PTR)Nls->UnicodeCodePageData -
                (ULONG_PTR)Nls->AnsiCodePageData);

    RtlInitNlsTables(Nls->AnsiCodePageData,
                     Nls->OemCodePageData,
                     Nls->UnicodeCodePageData,
                     &ExpNlsTableInfo);

    RtlResetRtlTranslations(&ExpNlsTableInfo);

    return TRUE;
}

/*
 * The loader's copy of the tables goes away with the loader block, so phase 1
 * moves them to pool for the kernel and into a section every process maps.
 */
CODE_SEG("INIT")
static
BOOLEAN
ExpInitNlsSection(VOID)
{
    LARGE_INTEGER SectionSize;
    PVOID SectionBase = NULL;
    SIZE_T ViewSize = 0;
    PUCHAR Tables;
    NTSTATUS Status;

    if (ExpNlsTableBase == NULL || ExpNlsTableSize == 0) return FALSE;

    Tables = ExAllocatePoolWithTag(NonPagedPool, ExpNlsTableSize, TAG_INIT);
    if (Tables == NULL) return FALSE;

    RtlCopyMemory(Tables, ExpNlsTableBase, ExpNlsTableSize);
    ExpNlsTableBase = Tables;

    RtlInitNlsTables((PUSHORT)(Tables + ExpAnsiCodePageDataOffset),
                     (PUSHORT)(Tables + ExpOemCodePageDataOffset),
                     (PUSHORT)(Tables + ExpUnicodeCaseTableDataOffset),
                     &ExpNlsTableInfo);
    RtlResetRtlTranslations(&ExpNlsTableInfo);

    SectionSize.QuadPart = ExpNlsTableSize;

    Status = MmCreateSection(&ExpNlsSectionPointer,
                             SECTION_ALL_ACCESS,
                             NULL,
                             &SectionSize,
                             PAGE_READWRITE,
                             SEC_COMMIT,
                             NULL,
                             NULL);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = MmMapViewInSystemSpace(ExpNlsSectionPointer, &SectionBase, &ViewSize);
    if (!NT_SUCCESS(Status)) return FALSE;

    RtlCopyMemory(SectionBase, Tables, ExpNlsTableSize);

    MmUnmapViewInSystemSpace(SectionBase);

    return TRUE;
}

/* The current directory buffer a user-mode parameter block carries: MAX_PATH characters */
#define EXP_CURRENT_DIRECTORY_LENGTH (260 * sizeof(WCHAR))

/*
 * Places one string of the parameter block at the cursor and moves the cursor
 * past the room it was given.
 */
CODE_SEG("INIT")
static
VOID
ExpPlaceParameterString(
    _Inout_ PUCHAR *Cursor,
    _Out_ PUNICODE_STRING Destination,
    _In_opt_ PCUNICODE_STRING Source,
    _In_ USHORT MaximumLength)
{
    Destination->Length = (Source != NULL) ? Source->Length : 0;
    Destination->MaximumLength = MaximumLength;
    Destination->Buffer = (MaximumLength != 0) ? (PWCHAR)*Cursor : NULL;

    if (Destination->Length != 0)
        RtlCopyMemory(Destination->Buffer, Source->Buffer, Destination->Length);

    if (MaximumLength >= Destination->Length + sizeof(WCHAR))
        Destination->Buffer[Destination->Length / sizeof(WCHAR)] = UNICODE_NULL;

    *Cursor += ALIGN_UP_BY(MaximumLength, sizeof(PVOID));
}

/*
 * The parameter block of the first process. There is no process heap here, so
 * it is laid out in pool the way a user-mode caller would get it: normalized,
 * strings after the header, the current directory given a MAX_PATH buffer.
 */
CODE_SEG("INIT")
static
PRTL_USER_PROCESS_PARAMETERS
ExpCreateInitialProcessParameters(
    _In_ PCUNICODE_STRING ImagePathName,
    _In_ PCUNICODE_STRING CurrentDirectory)
{
    PRTL_USER_PROCESS_PARAMETERS Parameters;
    PUNICODE_STRING DosPath;
    PUCHAR Cursor;
    ULONG Length;

    if (CurrentDirectory->Length + 2 * sizeof(WCHAR) > EXP_CURRENT_DIRECTORY_LENGTH)
        return NULL;

    Length = sizeof(RTL_USER_PROCESS_PARAMETERS) +
             EXP_CURRENT_DIRECTORY_LENGTH +
             ALIGN_UP_BY(CurrentDirectory->Length + sizeof(WCHAR), sizeof(PVOID)) +
             2 * ALIGN_UP_BY(ImagePathName->Length + sizeof(WCHAR), sizeof(PVOID)) +
             3 * ALIGN_UP_BY(sizeof(WCHAR), sizeof(PVOID));

    Parameters = ExAllocatePoolZero(PagedPool, Length, TAG_INIT);
    if (Parameters == NULL) return NULL;

    Parameters->MaximumLength = Length;
    Parameters->Length = Length;
    Parameters->Flags = RTL_USER_PROCESS_PARAMETERS_NORMALIZED;

    Cursor = (PUCHAR)(Parameters + 1);

    ExpPlaceParameterString(&Cursor,
                            &Parameters->CurrentDirectory.DosPath,
                            CurrentDirectory,
                            EXP_CURRENT_DIRECTORY_LENGTH);

    DosPath = &Parameters->CurrentDirectory.DosPath;
    if (DosPath->Length != 0 &&
        DosPath->Buffer[DosPath->Length / sizeof(WCHAR) - 1] != OBJ_NAME_PATH_SEPARATOR)
    {
        DosPath->Buffer[DosPath->Length / sizeof(WCHAR)] = OBJ_NAME_PATH_SEPARATOR;
        DosPath->Buffer[DosPath->Length / sizeof(WCHAR) + 1] = UNICODE_NULL;
        DosPath->Length += sizeof(WCHAR);
    }

    /* The loader searches the system directory for what the first process imports */
    ExpPlaceParameterString(&Cursor, &Parameters->DllPath, CurrentDirectory,
                            CurrentDirectory->Length + sizeof(WCHAR));
    ExpPlaceParameterString(&Cursor, &Parameters->ImagePathName, ImagePathName,
                            ImagePathName->Length + sizeof(WCHAR));
    ExpPlaceParameterString(&Cursor, &Parameters->CommandLine, ImagePathName,
                            ImagePathName->Length + sizeof(WCHAR));
    ExpPlaceParameterString(&Cursor, &Parameters->WindowTitle, NULL, sizeof(WCHAR));
    ExpPlaceParameterString(&Cursor, &Parameters->DesktopInfo, NULL, sizeof(WCHAR));
    ExpPlaceParameterString(&Cursor, &Parameters->ShellInfo, NULL, sizeof(WCHAR));
    ExpPlaceParameterString(&Cursor, &Parameters->RuntimeData, NULL, 0);

    return Parameters;
}

/*
 * The environment the first process starts with: where the system lives and
 * where to look for programs, the two things it needs before it reads the
 * rest from the registry. It is built in the address space of the current
 * process, where the code that copies it into the new one can measure it.
 */
CODE_SEG("INIT")
static
NTSTATUS
ExpCreateInitialEnvironment(
    _Out_ PVOID *Environment,
    _Out_ PSIZE_T EnvironmentSize)
{
    UNICODE_STRING Block;
    PVOID Base = NULL;
    SIZE_T Size = PAGE_SIZE;
    NTSTATUS Status;

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &Base,
                                     0,
                                     &Size,
                                     MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status)) return Status;

    /* The memory comes zeroed: skipping a character leaves a terminator, two end the block */
    Block.Buffer = Base;
    Block.Length = 0;
    Block.MaximumLength = (USHORT)min(Size - 2 * sizeof(WCHAR), MAXUSHORT - 1);

    Status = RtlAppendUnicodeToString(&Block, L"Path=");
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeStringToString(&Block, &NtSystemRoot);
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeToString(&Block, L"\\System32");
    if (NT_SUCCESS(Status))
    {
        UNICODE_STRING Drive;

        /* The system root starts with its drive letter and colon */
        ASSERT(NtSystemRoot.Length >= 2 * sizeof(WCHAR) && NtSystemRoot.Buffer[1] == L':');
        Drive.Buffer = NtSystemRoot.Buffer;
        Drive.Length = Drive.MaximumLength = 2 * sizeof(WCHAR);

        Block.Length += sizeof(WCHAR);
        Status = RtlAppendUnicodeToString(&Block, L"SystemDrive=");
        if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeStringToString(&Block, &Drive);
    }
    if (NT_SUCCESS(Status))
    {
        Block.Length += sizeof(WCHAR);
        Status = RtlAppendUnicodeToString(&Block, L"SystemRoot=");
    }
    if (NT_SUCCESS(Status)) Status = RtlAppendUnicodeStringToString(&Block, &NtSystemRoot);

    if (!NT_SUCCESS(Status))
    {
        Size = 0;
        ZwFreeVirtualMemory(NtCurrentProcess(), &Base, &Size, MEM_RELEASE);
        return Status;
    }

    *Environment = Base;
    *EnvironmentSize = Size;
    return STATUS_SUCCESS;
}

/*
 * Starts the one process the system starts for itself. Everything else that
 * runs is started by it, so if this fails there is nothing more to do.
 */
CODE_SEG("INIT")
static
VOID
ExpLoadInitialProcess(
    _Out_ PRTL_USER_PROCESS_INFORMATION ProcessInformation)
{
    PRTL_USER_PROCESS_PARAMETERS Parameters;
    UNICODE_STRING ImageName;
    UNICODE_STRING SystemDirectory =
        RTL_CONSTANT_STRING(L"\\SystemRoot\\System32");
    ANSI_STRING AnsiImageName;
    PVOID Environment = NULL;
    SIZE_T EnvironmentSize = 0;
    NTSTATUS Status;

    RtlInitAnsiString(&AnsiImageName, NtInitialUserProcessBuffer);

    Status = RtlAnsiStringToUnicodeString(&ImageName, &AnsiImageName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(SESSION1_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }

    Parameters = ExpCreateInitialProcessParameters(&ImageName, &SystemDirectory);
    if (Parameters == NULL)
    {
        RtlFreeUnicodeString(&ImageName);
        KeBugCheckEx(SESSION1_INITIALIZATION_FAILED, STATUS_INSUFFICIENT_RESOURCES, 0, 0, 0);
    }

    Status = ExpCreateInitialEnvironment(&Environment, &EnvironmentSize);
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(SESSION1_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }

    Parameters->Environment = Environment;
    Parameters->EnvironmentSize = EnvironmentSize;

    Status = RtlCreateUserProcess(&ImageName,
                                  OBJ_CASE_INSENSITIVE,
                                  Parameters,
                                  NULL,
                                  NULL,
                                  NULL,
                                  FALSE,
                                  NULL,
                                  NULL,
                                  ProcessInformation);

    ExFreePoolWithTag(Parameters, TAG_INIT);
    RtlFreeUnicodeString(&ImageName);

    EnvironmentSize = 0;
    ZwFreeVirtualMemory(NtCurrentProcess(), &Environment, &EnvironmentSize, MEM_RELEASE);

    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(SESSION1_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The executive's own part of a phase. Everything here is the executive's to
 * set up; the other subsystems are started around it by the phases below.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitSystemPhase0(VOID)
{
    ExpInitializeTime();
    ExpInitializeSystemInformation();

    if (!ExpInitializeEventImplementation())
    {
        return FALSE;
    }

    if (!ExpInitializeKeyedEventImplementation())
    {
        return FALSE;
    }

    if (!ExpInitializeSemaphoreImplementation())
    {
        return FALSE;
    }

    if (!ExpInitializeMutantImplementation())
    {
        return FALSE;
    }

    if (!ExpInitializeTimerImplementation())
    {
        return FALSE;
    }

    if (!ExpInitializeProfileImplementation())
    {
        return FALSE;
    }

    if (!ExpInitializeCallbacks())
    {
        return FALSE;
    }

    if (!ExpUuidInitialization())
    {
        return FALSE;
    }

    if (!ExLuidInitialization())
    {
        return FALSE;
    }

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitSystemPhase1(VOID)
{
    ExpInitializeWorkerThreads();
    ExpInitializeSecureBoot();

    if (!ExpInitializeCallbackObjects())
    {
        return FALSE;
    }

    if (!ExpWnfInitialization())
    {
        return FALSE;
    }

    if (!ExpWin32kInit())
    {
        return FALSE;
    }

    if (!ExpInitializeReserveObjectTypes())
    {
        return FALSE;
    }

    if (!ExpInitializePartitionImplementation())
    {
        return FALSE;
    }

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExInitSystem(VOID)
{
    if (ExpInitializationPhase == 0)
    {
        return ExpInitSystemPhase0();
    }

    if (ExpInitializationPhase == 1)
    {
        return ExpInitSystemPhase1();
    }

    KeBugCheckEx(UNEXPECTED_INITIALIZATION_CALL,
                 0,
                 ExpInitializationPhase,
                 0,
                 0);
}

/*
 * The first phase. Only as much of the system is brought up as is needed to
 * have a thread of its own, and the rest is done on that thread.
 */
CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeExecutive(
    _In_ ULONG Cpu,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    HANDLE Handle;

    /*
     * A processor other than the first has nothing of the system to bring up;
     * it only needs the HAL to know about it and its own pool lists.
     */
    if (Cpu != 0)
    {
        if (!HalInitSystem(0, LoaderBlock))
        {
            KeBugCheck(HAL_INITIALIZATION_FAILED);
        }

        ExInitPoolLookasidePointers();
        return;
    }

    ExpInitializationPhase = 0;

    if (!ExpIsLoaderValid(LoaderBlock))
    {
        KeBugCheckEx(MISMATCHED_HAL,
                     3,
                     LoaderBlock->Extension->Size,
                     LoaderBlock->OsMajorVersion,
                     LoaderBlock->OsMinorVersion);
    }

    ExpReadLoadOptions(LoaderBlock);
    ExpFirmwareType = LoaderBlock->FirmwareInformation.FirmwareTypeEfi ? FirmwareTypeUefi : FirmwareTypeBios;
    ExpBootEnvironmentInformation.BootIdentifier = LoaderBlock->Extension->BootIdentifier;
    /* The boot path is read through the code page tables */
    if (!ExpInitNls(LoaderBlock))
    {
        KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    }
    ExpInitializeSystemRoot(LoaderBlock);
    ExpInitializeImageNumbers(LoaderBlock);

    SharedUserData->NtProductType = NtProductWinNt;
    SharedUserData->ProductTypeIsValid = TRUE;
    SharedUserData->NtMajorVersion = NtMajorVersion;
    SharedUserData->NtMinorVersion = NtMinorVersion;
    SharedUserData->NtBuildNumber = NtBuildNumber & 0xFFFF;

    /* The tests tell this kernel from Windows by this value in the unused end of the shared page */
    *(PULONG)((ULONG_PTR)SharedUserData + PAGE_SIZE - sizeof(ULONG)) = 0x8EAC705;

    if (!HalInitSystem(0, LoaderBlock))
    {
        KeBugCheck(HAL_INITIALIZATION_FAILED);
    }

    /* The platform has given the clock period; the shared page turns ticks into milliseconds with it */
    ASSERT(KeMaximumIncrement != 0);
    ExpTickCountMultiplier = ExComputeTickCountMultiplier(KeMaximumIncrement);
    SharedUserData->TickCountMultiplier = ExpTickCountMultiplier;

    ExArchPostHalInitSystemPhase0();

    /*
     * The locks, handle tables and resources everything else reaches for come
     * first: the memory manager's own start already takes a resource.
     */
    ExpInitializePushLocks();
    ExpInitializeHandleTables();
    ExpResourceInitialization();

    /* Nothing may allocate before the memory manager has published its pool */
    if (!MmArmInitSystem(0, LoaderBlock))
    {
        KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    }

    ExpInitLookasideLists();
    ExInitPoolLookasidePointers();

    KdInitSystem(0, LoaderBlock);

    if (!MmInitSystem(0, LoaderBlock))
    {
        KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    }

    /* TODO: the images the loader brought are not announced to the debugger, nor checked for multiprocessor use */
    KiInitializeBugCheck();

    /* What the registry holds for the kernel is read from the hive the loader left in memory */
    CmGetSystemControlValues(LoaderBlock->RegistryBase, CmControlVector);
    NtGlobalFlag |= CmNtGlobalFlag;
    ExSuiteMask = ExpReadSuiteMask();
    MmWriteableSharedUserData->SuiteMask = ExSuiteMask;

    if (!ObInitSystem())
    {
        KeBugCheck(OBJECT_INITIALIZATION_FAILED);
    }

    if (!ExInitSystem())
    {
        KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    }

    if (!SeInitSystem())
    {
        KeBugCheck(SECURITY_INITIALIZATION_FAILED);
    }

    if (!PsInitSystem(LoaderBlock))
    {
        KeBugCheck(PROCESS_INITIALIZATION_FAILED);
    }

    if (!PoInitSystem(0))
    {
        KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    }

    HeadlessInit(LoaderBlock);
    XIPInit(LoaderBlock);

    /*
     * Everything that is left may have to wait for something, which this
     * processor cannot do until it has a thread of its own, so the rest of the
     * work is handed to one.
     */
    if (!NT_SUCCESS(PsCreateSystemThread(&Handle,
                                         THREAD_ALL_ACCESS,
                                         NULL,
                                         NULL,
                                         NULL,
                                         Phase1Initialization,
                                         LoaderBlock)))
    {
        KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    }

    ObCloseHandle(Handle, KernelMode);
}


/* What the screen shows while the system starts: nothing, or the logo with its progress */
CODE_SEG("INIT")
static
VOID
ExpStartBootDisplay(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PCSTR Options = LoaderBlock->LoadOptions;

    if (!InbvIsBootDriverInstalled()) return;

    if (ExpFindLoadOption(Options, "NOGUIBOOT") != NULL)
    {
        InbvEnableBootDriver(FALSE);
        return;
    }

    if (KdScreenIsBootLog())
        return;

    DisplayBootBitmap();

    /* TODO: SOS only enables the status text; nothing names the drivers as they load */
    if (ExpFindLoadOption(Options, "SOS") != NULL)
    {
        InbvEnableDisplayString(TRUE);
    }
}

/*
 * The second phase, which runs on a thread and may therefore wait. Everything
 * the system has that a user can reach is set up here.
 */

CODE_SEG("INIT")
VOID
NTAPI
Phase1InitializationDiscard(
    _In_ PVOID Context)
{
    PLOADER_PARAMETER_BLOCK LoaderBlock = Context;
    RTL_USER_PROCESS_INFORMATION ProcessInformation;
    LARGE_INTEGER BootTime;

    ExpInitializationPhase = 1;

    KeQuerySystemTime(&BootTime);

    if (!HalInitSystem(1, LoaderBlock))
    {
        KeBugCheck(HAL1_INITIALIZATION_FAILED);
    }

    ExArchPostHalInitSystemPhase1();

    /* The firmware's framebuffer stays the display until a display driver takes it over */
    InbvDriverInitialize(LoaderBlock, 0);
    ExpStartBootDisplay(LoaderBlock);

    if (!PoInitSystem(1))
    {
        KeBugCheck(PHASE1_INITIALIZATION_FAILED);
    }

    KeStartAllProcessors();

    if (!HalAllProcessorsStarted())
    {
        KeBugCheck(HAL1_INITIALIZATION_FAILED);
    }

    KiExpandInitialProcessAffinities();

    KiInitializeProcessorEfficiency();

    /*
     * The namespace comes before anything that names what it makes, which is
     * everything below: a name is looked up from the root, and until this
     * returns there is no root to look up from.
     */
    if (!ObInitSystem())
    {
        KeBugCheck(OBJECT1_INITIALIZATION_FAILED);
    }

    if (!ExInitSystem())
    {
        KeBugCheck(PHASE1_INITIALIZATION_FAILED);
    }

    if (!KeInitSystem())
    {
        KeBugCheck(PHASE1_INITIALIZATION_FAILED);
    }

    KdInitSystem(1, LoaderBlock);

    if (!SeInitSystem())
    {
        KeBugCheck(SECURITY1_INITIALIZATION_FAILED);
    }

    /*
     * The memory manager finishes after the HAL's second turn, because the HAL
     * moves its own registers to kernel addresses there and the architecture
     * hook above retires the early mappings it was using until then.
     */
    if (!MmInitSystem(1, LoaderBlock))
    {
        KeBugCheck(PHASE1_INITIALIZATION_FAILED);
    }

    if (!PsInitSystem(LoaderBlock))
    {
        KeBugCheck(PROCESS1_INITIALIZATION_FAILED);
    }

    DbgkInitialize();

    if (!LpcInitSystem())
    {
        KeBugCheck(LPC_INITIALIZATION_FAILED);
    }

    /* The reference monitor serves its command port, so it starts once ports exist */
    if (!SeRmInitPhase1())
    {
        KeBugCheck(SECURITY_INITIALIZATION_FAILED);
    }

    /* The registry keeps its transactions with the transaction manager, whose objects come first */
    if (!TmpInitSystem())
    {
        KeBugCheck(PHASE1_INITIALIZATION_FAILED);
    }

    if (!CmInitSystem1())
    {
        KeBugCheck(CONFIG_INITIALIZATION_FAILED);
    }

    if (!FsRtlInitSystem())
    {
        KeBugCheck(FILE_INITIALIZATION_FAILED);
    }

    if (!CcInitializeCacheManager())
    {
        KeBugCheck(CACHE_INITIALIZATION_FAILED);
    }

    /* The link names the ARC boot device; Io reassigns it once that device exists. */
    if (!ExpCreateSystemRootLink(LoaderBlock))
    {
        KeBugCheck(SYMBOLIC_INITIALIZATION_FAILED);
    }

    /* What the HAL holds for itself is on record before any driver asks for resources */
    RtlInitializeRangeListPackage();
    HalReportResourceUsage();
    KdDebuggerInitialize1(LoaderBlock);
    KdStartPrintWriter();

    if (ExpFindLoadOption(LoaderBlock->LoadOptions, "SMPDIAG") != NULL)
    {
        SmpDbgEnabled = TRUE;
        SmpDbgStartWatchdog();
    }

    KseInitialize();

    if (!IoInitSystem(LoaderBlock))
    {
        KeBugCheck(IO1_INITIALIZATION_FAILED);
    }

    ExRefreshTimeZoneInformation(&BootTime);

    if (!ExpRealTimeIsUniversal)
    {
        ExUpdateSystemTimeFromCmos(FALSE, 0);
    }

    if (!PoInitSystem(2))
    {
        KeBugCheck(PHASE1_INITIALIZATION_FAILED);
    }

    if (!ExpInitNlsSection())
    {
        KeBugCheck(PHASE1_INITIALIZATION_FAILED);
    }

    if (!NT_SUCCESS(PsLocateSystemDll()))
    {
        KeBugCheck(PROCESS1_INITIALIZATION_FAILED);
    }

    ExpLoadInitialProcess(&ProcessInformation);

    /* The first process is created with its thread held; it starts here */
    ZwResumeThread(ProcessInformation.ThreadHandle, NULL);

    FinalizeBootLogo();

    if (!PoInitSystem(3))
    {
        KeBugCheck(PHASE1_INITIALIZATION_FAILED);
    }

    ObCloseHandle(ProcessInformation.ThreadHandle, KernelMode);
    ObCloseHandle(ProcessInformation.ProcessHandle, KernelMode);

    MmArmInitSystem(2, LoaderBlock);

    KeLoaderBlock = NULL;
    MmFreeLoaderBlock(LoaderBlock);

    ExpInitializationPhase = 2;
}


CODE_SEG("INIT")
VOID
NTAPI
Phase1Initialization(
    _In_ PVOID Context)
{
    Phase1InitializationDiscard(Context);

    /*
     * Nothing further is wanted from this thread, and the pages this phase took
     * are given back once it is gone.
     */
    MmZeroPageThread();
}

/* EOF */
