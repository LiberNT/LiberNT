/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the whole registry shares: its state, its lock and its quota
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* GLOBALS ********************************************************************/

ULONG CmpTraceLevel = 0;

LIST_ENTRY CmpHiveListHead;
EX_PUSH_LOCK CmpHiveListHeadLock;
EX_PUSH_LOCK CmpLoadHiveLock;

LIST_ENTRY CmpSelfHealQueueListHead;
KGUARDED_MUTEX CmpSelfHealQueueLock;

HANDLE CmpRegistryRootHandle = NULL;
PCM_KEY_BODY CmpRegistryRootObject = NULL;
PCM_KEY_BODY CmpRegistryNamespaceRoot = NULL;

PCMHIVE CmiVolatileHive = NULL;
CMHIVE CmControlHive;

PEPROCESS CmpSystemProcess = NULL;

BOOLEAN CmpSpecialBootCondition = FALSE;
BOOLEAN CmpFlushOnLockRelease = FALSE;
BOOLEAN CmpShareSystemHives = FALSE;
BOOLEAN CmpMiniNTBoot = FALSE;
BOOLEAN CmpNoVolatileCreates = TRUE;
BOOLEAN CmpNoWrite = FALSE;
BOOLEAN CmpForceForceFlush = FALSE;
BOOLEAN CmpWasSetupBoot = FALSE;
BOOLEAN CmpProfileLoaded = FALSE;
BOOLEAN CmpSelfHeal = TRUE;
BOOLEAN HvShutdownComplete = FALSE;
BOOLEAN CmpRegistryLockCreated = FALSE;

ULONG CmpVolatileBoot = 0;
ULONG CmpBootType = 0;
ULONG CmSelfHeal = 1;

ULONG CmpHashTableSize = CMP_HASH_TABLE_DEFAULT;
ULONG CmpDelayedCloseSize = CMP_DELAYED_CLOSE_MAX;

ULONG CmpGlobalQuota = 0;
ULONG CmpGlobalQuotaAllowed = 0;

ULONG CmpFlushStarveWriters = 0;

PVOID CmpRegistryLockCaller = NULL;
PVOID CmpRegistryLockCallerCaller = NULL;

UNICODE_STRING CmSymbolicLinkValueName =
    RTL_CONSTANT_STRING(L"SymbolicLinkValue");
UNICODE_STRING CmpSystemStartOptions = {0, 0, NULL};
UNICODE_STRING CmpLoadOptions = {0, 0, NULL};

UNICODE_STRING CmpSystemFileName = RTL_CONSTANT_STRING(L"SYSTEM");
UNICODE_STRING CmpRegistryMachineHardware =
    RTL_CONSTANT_STRING(L"\\REGISTRY\\MACHINE\\HARDWARE");
UNICODE_STRING CmpRegistryMachineSystem =
    RTL_CONSTANT_STRING(L"\\REGISTRY\\MACHINE\\SYSTEM");
UNICODE_STRING CmpRegistryMachineSystemCurrentControlSet =
    RTL_CONSTANT_STRING(L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET");

ULONG DummyData = 0;

ULONG CmNtGlobalFlag = 0;
ULONG CmTsAppCompat = 0;

WCHAR CmDefaultLanguageId[12] = {0};
ULONG CmDefaultLanguageIdLength = sizeof(CmDefaultLanguageId);
ULONG CmDefaultLanguageIdType = REG_NONE;

WCHAR CmInstallUILanguageId[12] = {0};
ULONG CmInstallUILanguageIdLength = sizeof(CmInstallUILanguageId);
ULONG CmInstallUILanguageIdType = REG_NONE;

WCHAR CmSuiteBuffer[128] = {0};
ULONG CmSuiteBufferLength = sizeof(CmSuiteBuffer);
ULONG CmSuiteBufferType = REG_NONE;

/* PRIVATE DATA ***************************************************************/

static ULONG CmpGlobalFlagLength = sizeof(ULONG);
static ULONG CmpGlobalQuotaLength = sizeof(ULONG);
static ULONG CmpTsAppCompatLength = sizeof(ULONG);
static ULONG CmpIgnoredLength = sizeof(ULONG);

/* GLOBALS ********************************************************************/

CM_SYSTEM_CONTROL_VECTOR CmControlVector[] =
{
    {
        L"Session Manager",
        L"GlobalFlag",
        &CmNtGlobalFlag,
        &CmpGlobalFlagLength,
        &DummyData
    },
    {
        L"Session Manager",
        L"RegistrySizeLimit",
        &CmpGlobalQuota,
        &CmpGlobalQuotaLength,
        &DummyData
    },
    {
        L"Session Manager\\Memory Management",
        L"PagedPoolQuota",
        &DummyData,
        &CmpIgnoredLength,
        &DummyData
    },
    {
        L"Nls\\Language",
        L"Default",
        CmDefaultLanguageId,
        &CmDefaultLanguageIdLength,
        &CmDefaultLanguageIdType
    },
    {
        L"Nls\\Language",
        L"InstallLanguage",
        CmInstallUILanguageId,
        &CmInstallUILanguageIdLength,
        &CmInstallUILanguageIdType
    },
    {
        L"ProductOptions",
        L"ProductSuite",
        CmSuiteBuffer,
        &CmSuiteBufferLength,
        &CmSuiteBufferType
    },
    {
        L"Terminal Server",
        L"TSAppCompat",
        &CmTsAppCompat,
        &CmpTsAppCompatLength,
        &DummyData
    },
    {
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    }
};

UNICODE_STRING CmTypeName[MaximumType + 1] =
{
    RTL_CONSTANT_STRING(L"System"),
    RTL_CONSTANT_STRING(L"CentralProcessor"),
    RTL_CONSTANT_STRING(L"FloatingPointProcessor"),
    RTL_CONSTANT_STRING(L"PrimaryICache"),
    RTL_CONSTANT_STRING(L"PrimaryDCache"),
    RTL_CONSTANT_STRING(L"SecondaryICache"),
    RTL_CONSTANT_STRING(L"SecondaryDCache"),
    RTL_CONSTANT_STRING(L"SecondaryCache"),
    RTL_CONSTANT_STRING(L"EisaAdapter"),
    RTL_CONSTANT_STRING(L"TcAdapter"),
    RTL_CONSTANT_STRING(L"ScsiAdapter"),
    RTL_CONSTANT_STRING(L"DtiAdapter"),
    RTL_CONSTANT_STRING(L"MultifunctionAdapter"),
    RTL_CONSTANT_STRING(L"DiskController"),
    RTL_CONSTANT_STRING(L"TapeController"),
    RTL_CONSTANT_STRING(L"CdRomController"),
    RTL_CONSTANT_STRING(L"WormController"),
    RTL_CONSTANT_STRING(L"SerialController"),
    RTL_CONSTANT_STRING(L"NetworkController"),
    RTL_CONSTANT_STRING(L"DisplayController"),
    RTL_CONSTANT_STRING(L"ParallelController"),
    RTL_CONSTANT_STRING(L"PointerController"),
    RTL_CONSTANT_STRING(L"KeyboardController"),
    RTL_CONSTANT_STRING(L"AudioController"),
    RTL_CONSTANT_STRING(L"OtherController"),
    RTL_CONSTANT_STRING(L"DiskPeripheral"),
    RTL_CONSTANT_STRING(L"FloppyDiskPeripheral"),
    RTL_CONSTANT_STRING(L"TapePeripheral"),
    RTL_CONSTANT_STRING(L"ModemPeripheral"),
    RTL_CONSTANT_STRING(L"MonitorPeripheral"),
    RTL_CONSTANT_STRING(L"PrinterPeripheral"),
    RTL_CONSTANT_STRING(L"PointerPeripheral"),
    RTL_CONSTANT_STRING(L"KeyboardPeripheral"),
    RTL_CONSTANT_STRING(L"TerminalPeripheral"),
    RTL_CONSTANT_STRING(L"OtherPeripheral"),
    RTL_CONSTANT_STRING(L"LinePeripheral"),
    RTL_CONSTANT_STRING(L"NetworkPeripheral"),
    RTL_CONSTANT_STRING(L"SystemMemory"),
    RTL_CONSTANT_STRING(L"DockingInformation"),
    RTL_CONSTANT_STRING(L"RealModeIrqRoutingTable"),
    RTL_CONSTANT_STRING(L"RealModePCIEnumeration"),
    RTL_CONSTANT_STRING(L"Unknown")
};

UNICODE_STRING CmClassName[MaximumClass + 1] =
{
    RTL_CONSTANT_STRING(L"System"),
    RTL_CONSTANT_STRING(L"Processor"),
    RTL_CONSTANT_STRING(L"Cache"),
    RTL_CONSTANT_STRING(L"Adapter"),
    RTL_CONSTANT_STRING(L"Controller"),
    RTL_CONSTANT_STRING(L"Peripheral"),
    RTL_CONSTANT_STRING(L"Memory"),
    RTL_CONSTANT_STRING(L"Unknown")
};

CMP_MF_TYPE CmpMultifunctionTypes[] =
{
    {"ISA", Isa, 0},
    {"EISA", Eisa, 0},
    {"MCA", MicroChannel, 0},
    {"PCI", PCIBus, 0},
    {"VME", VMEBus, 0},
    {"PCMCIA", PCMCIABus, 0},
    {"CBUS", CBus, 0},
    {"MPIBUS", MPIBus, 0},
    {"MPSA", MPSABus, 0},
    {"PROCESSORINTERNAL", ProcessorInternal, 0},
    {"INTERNAL", Internal, 0},
    {"PNPISA", PNPISABus, 0},
    {"ACPI", ACPIBus, 0},
    {NULL, InterfaceTypeUndefined, 0}
};

USHORT CmpUnknownBusCount = 0;
ULONG CmpTypeCount[MaximumType + 1] = {0};

ULONG CmpConfigurationAreaSize =
    (ULONG)FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList) +
    PAGE_SIZE * 2;
PCM_FULL_RESOURCE_DESCRIPTOR CmpConfigurationData = NULL;

/* PRIVATE DATA ***************************************************************/

static ERESOURCE CmpRegistryLock;

extern LCID PsDefaultThreadLocaleId;
extern LCID PsDefaultSystemLocaleId;

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
CmpInitRegistryLock(VOID)
{
    InitializeListHead(&CmpHiveListHead);
    InitializeListHead(&CmpSelfHealQueueListHead);
    KeInitializeGuardedMutex(&CmpSelfHealQueueLock);
    ExInitializePushLock(&CmpHiveListHeadLock);
    ExInitializePushLock(&CmpLoadHiveLock);
    ExInitializeResourceLite(&CmpRegistryLock);
    CmpRegistryLockCreated = TRUE;
}

/* While a flush builds its dirty vector, a reader jumps the writers' queue */
VOID
NTAPI
CmpLockRegistry(VOID)
{
    KeEnterCriticalRegion();

    if (CmpFlushStarveWriters != 0)
    {
        ExAcquireSharedStarveExclusive(&CmpRegistryLock, TRUE);
        return;
    }

    ExAcquireResourceSharedLite(&CmpRegistryLock, TRUE);
}

VOID
NTAPI
CmpLockRegistryExclusive(VOID)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&CmpRegistryLock, TRUE);

    CmpRegistryLockCallerCaller = NULL;
    CmpRegistryLockCaller = NULL;
    CmpFlushOnLockRelease = FALSE;
}

/* Whatever asked to be on disk gets there before anyone else sees the hive */
VOID
NTAPI
CmpUnlockRegistry(VOID)
{
    if (CmpFlushOnLockRelease)
    {
        CmpDoFlushAll(TRUE);
        CmpFlushOnLockRelease = FALSE;
    }

    ExReleaseResourceLite(&CmpRegistryLock);
    KeLeaveCriticalRegion();
}

BOOLEAN
NTAPI
CmpTestRegistryLock(VOID)
{
    if (!CmpRegistryLockCreated) return FALSE;
    return ExIsResourceAcquiredSharedLite(&CmpRegistryLock) != 0;
}

BOOLEAN
NTAPI
CmpTestRegistryLockExclusive(VOID)
{
    if (!CmpRegistryLockCreated) return FALSE;
    return ExIsResourceAcquiredExclusiveLite(&CmpRegistryLock);
}

VOID
NTAPI
CmpBeginStarveWriters(VOID)
{
    InterlockedIncrement((PLONG)&CmpFlushStarveWriters);
}

VOID
NTAPI
CmpEndStarveWriters(VOID)
{
    InterlockedDecrement((PLONG)&CmpFlushStarveWriters);
}

/* Nothing is charged against the quota until its limit has been read */
VOID
NTAPI
CmpSetGlobalQuotaAllowed(VOID)
{
    CmpGlobalQuotaAllowed = CmpGlobalQuota;
}
