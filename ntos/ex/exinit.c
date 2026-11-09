/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the executive up, in the order its parts need
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
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

    /* Everything after this point may say \SystemRoot and be understood. */
    NtSystemRoot.Length = 0;
    RtlAppendUnicodeToString(&NtSystemRoot, L"\\SystemRoot");

    return TRUE;
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
 * Starts the one process the system starts for itself. Everything else that
 * runs is started by it, so if this fails there is nothing more to do.
 */
CODE_SEG("INIT")
static
VOID
ExpLoadInitialProcess(
    _Out_ PRTL_USER_PROCESS_INFORMATION ProcessInformation)
{
    PRTL_USER_PROCESS_PARAMETERS Parameters = NULL;
    UNICODE_STRING ImageName;
    UNICODE_STRING CommandLine;
    UNICODE_STRING SystemDirectory =
        RTL_CONSTANT_STRING(L"\\SystemRoot\\System32");
    ANSI_STRING AnsiCommandLine;
    NTSTATUS Status;

    RtlInitAnsiString(&AnsiCommandLine, NtInitialUserProcessBuffer);

    Status = RtlAnsiStringToUnicodeString(&CommandLine, &AnsiCommandLine, TRUE);
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(SESSION1_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }

    ImageName = CommandLine;

    Status = RtlCreateProcessParameters(&Parameters,
                                        &ImageName,
                                        NULL,
                                        &SystemDirectory,
                                        &CommandLine,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(&CommandLine);
        KeBugCheckEx(SESSION1_INITIALIZATION_FAILED, Status, 0, 0, 0);
    }

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

    RtlDestroyProcessParameters(Parameters);
    RtlFreeUnicodeString(&CommandLine);

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

    if (!ExpInitializeEventPairImplementation())
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

    if (!ExpInitializeCallbackObjects())
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

    SharedUserData->NtProductType = NtProductWinNt;
    SharedUserData->ProductTypeIsValid = TRUE;
    SharedUserData->NtMajorVersion = NtMajorVersion;
    SharedUserData->NtMinorVersion = NtMinorVersion;

    if (!HalInitSystem(0, LoaderBlock))
    {
        KeBugCheck(HAL_INITIALIZATION_FAILED);
    }

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

    if (!ExpInitNls(LoaderBlock))
    {
        KeBugCheck(PHASE0_INITIALIZATION_FAILED);
    }

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

    if (!PoInitSystem(1))
    {
        KeBugCheck(PHASE1_INITIALIZATION_FAILED);
    }

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

    if (!IoInitSystem(LoaderBlock))
    {
        KeBugCheck(IO1_INITIALIZATION_FAILED);
    }

    /* The device names the link is built from only exist once Io has run. */
    if (!ExpCreateSystemRootLink(LoaderBlock))
    {
        KeBugCheck(SYMBOLIC_INITIALIZATION_FAILED);
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

    if (!NT_SUCCESS(PsLocateSystemDll()))
    {
        KeBugCheck(PROCESS1_INITIALIZATION_FAILED);
    }

    ExpLoadInitialProcess(&ProcessInformation);

    if (!PoInitSystem(3))
    {
        KeBugCheck(PHASE1_INITIALIZATION_FAILED);
    }

    ObCloseHandle(ProcessInformation.ThreadHandle, KernelMode);
    ObCloseHandle(ProcessInformation.ProcessHandle, KernelMode);

    MmArmInitSystem(2, LoaderBlock);

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
