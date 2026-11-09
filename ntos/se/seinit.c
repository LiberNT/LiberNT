/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the security of the system up
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* What drivers are told about, filled once everything it names exists */
static SE_EXPORTS SepSeExports;

typedef struct _SEP_EXPORTED_PRIVILEGE
{
    ULONG Offset;
    const LUID *Privilege;
} SEP_EXPORTED_PRIVILEGE;

typedef struct _SEP_EXPORTED_SID
{
    ULONG Offset;
    PSID *Sid;
} SEP_EXPORTED_SID;

#define SEP_EXPORT_PRIVILEGE(n) {FIELD_OFFSET(SE_EXPORTS, n), &n}
#define SEP_EXPORT_SID(n) {FIELD_OFFSET(SE_EXPORTS, n), &n}

/* GLOBALS ********************************************************************/

PTOKEN SeAnonymousLogonTokenNoEveryone = NULL;
PTOKEN SeAnonymousLogonToken = NULL;
PSE_EXPORTS SeExports = &SepSeExports;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Hands the published structure the privileges and the identifiers the system
 * settled for itself, each one written where the structure says it belongs.
 */
static
CODE_SEG("INIT")
VOID
SepPublishExports(VOID)
{
    static const SEP_EXPORTED_PRIVILEGE Privileges[] =
    {
        SEP_EXPORT_PRIVILEGE(SeCreateTokenPrivilege),
        SEP_EXPORT_PRIVILEGE(SeAssignPrimaryTokenPrivilege),
        SEP_EXPORT_PRIVILEGE(SeLockMemoryPrivilege),
        SEP_EXPORT_PRIVILEGE(SeIncreaseQuotaPrivilege),
        SEP_EXPORT_PRIVILEGE(SeUnsolicitedInputPrivilege),
        SEP_EXPORT_PRIVILEGE(SeTcbPrivilege),
        SEP_EXPORT_PRIVILEGE(SeSecurityPrivilege),
        SEP_EXPORT_PRIVILEGE(SeTakeOwnershipPrivilege),
        SEP_EXPORT_PRIVILEGE(SeLoadDriverPrivilege),
        SEP_EXPORT_PRIVILEGE(SeCreatePagefilePrivilege),
        SEP_EXPORT_PRIVILEGE(SeIncreaseBasePriorityPrivilege),
        SEP_EXPORT_PRIVILEGE(SeSystemProfilePrivilege),
        SEP_EXPORT_PRIVILEGE(SeSystemtimePrivilege),
        SEP_EXPORT_PRIVILEGE(SeProfileSingleProcessPrivilege),
        SEP_EXPORT_PRIVILEGE(SeCreatePermanentPrivilege),
        SEP_EXPORT_PRIVILEGE(SeBackupPrivilege),
        SEP_EXPORT_PRIVILEGE(SeRestorePrivilege),
        SEP_EXPORT_PRIVILEGE(SeShutdownPrivilege),
        SEP_EXPORT_PRIVILEGE(SeDebugPrivilege),
        SEP_EXPORT_PRIVILEGE(SeAuditPrivilege),
        SEP_EXPORT_PRIVILEGE(SeSystemEnvironmentPrivilege),
        SEP_EXPORT_PRIVILEGE(SeChangeNotifyPrivilege),
        SEP_EXPORT_PRIVILEGE(SeRemoteShutdownPrivilege),
        SEP_EXPORT_PRIVILEGE(SeUndockPrivilege),
        SEP_EXPORT_PRIVILEGE(SeSyncAgentPrivilege),
        SEP_EXPORT_PRIVILEGE(SeEnableDelegationPrivilege),
        SEP_EXPORT_PRIVILEGE(SeManageVolumePrivilege),
        SEP_EXPORT_PRIVILEGE(SeImpersonatePrivilege),
        SEP_EXPORT_PRIVILEGE(SeCreateGlobalPrivilege),
        SEP_EXPORT_PRIVILEGE(SeRelabelPrivilege),
        SEP_EXPORT_PRIVILEGE(SeIncreaseWorkingSetPrivilege),
        SEP_EXPORT_PRIVILEGE(SeTimeZonePrivilege),
        SEP_EXPORT_PRIVILEGE(SeCreateSymbolicLinkPrivilege),
        SEP_EXPORT_PRIVILEGE(SeDelegateSessionUserImpersonatePrivilege),
        {FIELD_OFFSET(SE_EXPORTS, SeTrustedCredManAccessPrivilege),
         &SeTrustedCredmanPrivilege},
    };
    static const SEP_EXPORTED_SID Identifiers[] =
    {
        SEP_EXPORT_SID(SeNullSid),
        SEP_EXPORT_SID(SeWorldSid),
        SEP_EXPORT_SID(SeLocalSid),
        SEP_EXPORT_SID(SeCreatorOwnerSid),
        SEP_EXPORT_SID(SeCreatorGroupSid),
        SEP_EXPORT_SID(SeNtAuthoritySid),
        SEP_EXPORT_SID(SeDialupSid),
        SEP_EXPORT_SID(SeNetworkSid),
        SEP_EXPORT_SID(SeBatchSid),
        SEP_EXPORT_SID(SeInteractiveSid),
        SEP_EXPORT_SID(SeLocalSystemSid),
        SEP_EXPORT_SID(SeAliasAdminsSid),
        SEP_EXPORT_SID(SeAliasUsersSid),
        SEP_EXPORT_SID(SeAliasGuestsSid),
        SEP_EXPORT_SID(SeAliasPowerUsersSid),
        SEP_EXPORT_SID(SeAliasAccountOpsSid),
        SEP_EXPORT_SID(SeAliasSystemOpsSid),
        SEP_EXPORT_SID(SeAliasPrintOpsSid),
        SEP_EXPORT_SID(SeAliasBackupOpsSid),
        SEP_EXPORT_SID(SeAuthenticatedUsersSid),
        SEP_EXPORT_SID(SeRestrictedSid),
        SEP_EXPORT_SID(SeAnonymousLogonSid),
        SEP_EXPORT_SID(SeLocalServiceSid),
        SEP_EXPORT_SID(SeNetworkServiceSid),
        SEP_EXPORT_SID(SeIUserSid),
        SEP_EXPORT_SID(SeUntrustedMandatorySid),
        SEP_EXPORT_SID(SeLowMandatorySid),
        SEP_EXPORT_SID(SeMediumMandatorySid),
        SEP_EXPORT_SID(SeHighMandatorySid),
        SEP_EXPORT_SID(SeSystemMandatorySid),
        SEP_EXPORT_SID(SeOwnerRightsSid),
        SEP_EXPORT_SID(SeAllAppPackagesSid),
        SEP_EXPORT_SID(SeUserModeDriversSid),
        SEP_EXPORT_SID(SeProcTrustWinTcbSid),
        SEP_EXPORT_SID(SeTrustedInstallerSid),
    };
    PUCHAR Base = (PUCHAR)&SepSeExports;
    ULONG i;

    PAGED_CODE();

    RtlZeroMemory(&SepSeExports, sizeof(SepSeExports));

    for (i = 0; i < RTL_NUMBER_OF(Privileges); i++)
        *(PLUID)(Base + Privileges[i].Offset) = *Privileges[i].Privilege;

    for (i = 0; i < RTL_NUMBER_OF(Identifiers); i++)
        *(PSID *)(Base + Identifiers[i].Offset) = *Identifiers[i].Sid;

    /* TODO: the silo identifiers Windows 11 also publishes are left unset */
}

/*
 * The corner of the namespace the authority keeps, and the event it says it
 * is ready with. Both outlive the handles they are made with, because
 * everything that waits for the authority opens them by name.
 */
static
CODE_SEG("INIT")
BOOLEAN
SepPublishSecurityDirectory(VOID)
{
    UNICODE_STRING DirectoryName = RTL_CONSTANT_STRING(L"\\Security");
    UNICODE_STRING EventName = RTL_CONSTANT_STRING(L"LSA_AUTHENTICATION_INITIALIZED");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE DirectoryHandle;
    HANDLE EventHandle;
    NTSTATUS Status;

    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes,
                               &DirectoryName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT |
                                   OBJ_KERNEL_HANDLE,
                               NULL,
                               SePublicDefaultSd);

    Status = ZwCreateDirectoryObject(&DirectoryHandle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SE: could not open the security directory (0x%08lx)\n", Status);
        return FALSE;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &EventName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT |
                                   OBJ_KERNEL_HANDLE,
                               DirectoryHandle,
                               SePublicDefaultSd);

    Status = ZwCreateEvent(&EventHandle,
                           EVENT_ALL_ACCESS,
                           &ObjectAttributes,
                           NotificationEvent,
                           FALSE);

    ObCloseHandle(DirectoryHandle, KernelMode);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SE: could not publish the authority's event (0x%08lx)\n", Status);
        return FALSE;
    }

    ObCloseHandle(EventHandle, KernelMode);
    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Everything security rests on, settled before the process manager runs: who
 * the system knows of, what may be asked for, what protects an object by
 * default, and the token the system itself runs with.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitializationPhase0(VOID)
{
    PTOKEN SystemToken;

    PAGED_CODE();

    ExLuidInitialization();

    if (!SepInitSecurityIDs())
    {
        DPRINT1("SE: could not settle the identifiers the system knows\n");
        return FALSE;
    }

    SepInitPrivileges();

    if (!SepInitSystemDacls() || !SepInitSystemSds())
    {
        DPRINT1("SE: could not settle what protects an object by default\n");
        return FALSE;
    }

    SepPublishExports();

    ExInitializeResource(&SepSubjectContextLock);

    SepInitializeTokenImplementation();

    if (!SepRmInitializeSessions())
    {
        DPRINT1("SE: could not settle the logon sessions of the system\n");
        return FALSE;
    }

    /* The process this runs in is the system's own, and this is its token */
    SystemToken = SepCreateSystemProcessToken();
    if (SystemToken == NULL)
    {
        DPRINT1("SE: could not make the token the system runs with\n");
        return FALSE;
    }

    ObInitializeFastReference(&PsGetCurrentProcess()->Token, SystemToken);

    SeAnonymousLogonToken = SepCreateSystemAnonymousLogonToken();
    SeAnonymousLogonTokenNoEveryone =
        SepCreateSystemAnonymousLogonTokenNoEveryone();

    if (SeAnonymousLogonToken == NULL ||
        SeAnonymousLogonTokenNoEveryone == NULL)
    {
        DPRINT1("SE: could not make the tokens of the anonymous logon\n");
        return FALSE;
    }

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitializationPhase1(VOID)
{
    PAGED_CODE();

    return SepPublishSecurityDirectory();
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
SeInitSystem(VOID)
{
    PAGED_CODE();

    if (ExpInitializationPhase == 0) return SepInitializationPhase0();
    if (ExpInitializationPhase == 1) return SepInitializationPhase1();

    KeBugCheckEx(SECURITY_INITIALIZATION_FAILED,
                 STATUS_INVALID_PARAMETER,
                 ExpInitializationPhase,
                 0,
                 0);
}
