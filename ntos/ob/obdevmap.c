/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DOS device maps
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#define OBP_DEVICE_MAP_TAG 'mDbO'

ULONG ObpLUIDDeviceMapsDisabled;
ULONG ObpLUIDDeviceMapsEnabled;

/* THE MAP A PROCESS CARRIES **************************************************/

static
VOID
ObpReferenceDeviceMapBy(
    _In_ PDEVICE_MAP DeviceMap,
    _In_ LONG Count)
{
    InterlockedExchangeAdd((PLONG)&DeviceMap->ReferenceCount, Count);
}

static
PDEVICE_MAP
ObpReferenceProcessDeviceMap(
    _In_ PEPROCESS Process)
{
    EX_FAST_REF OldValue;
    PDEVICE_MAP DeviceMap;
    ULONG Cached;

    OldValue = ExAcquireFastReference(&Process->DeviceMap);
    DeviceMap = ExGetObjectFastReference(OldValue);
    Cached = ExGetCountFastReference(OldValue);

    if (DeviceMap == NULL || Cached > 1) return DeviceMap;

    if (Cached == 1)
    {
        /* That was the last cached one; buy a batch for whoever comes next */
        ObpReferenceDeviceMapBy(DeviceMap, MAX_FAST_REFS);
        if (!ExInsertFastReference(&Process->DeviceMap, DeviceMap))
            ObpReferenceDeviceMapBy(DeviceMap, -(LONG)MAX_FAST_REFS);

        return DeviceMap;
    }

    /* Nothing was cached, so hold the field still and charge one directly */
    PspLockProcessSecurityShared(Process);

    DeviceMap = ExGetObjectFastReference(Process->DeviceMap);
    if (DeviceMap != NULL) ObpReferenceDeviceMapBy(DeviceMap, 1);

    PspUnlockProcessSecurityShared(Process);
    return DeviceMap;
}

/*
 * Takes one reference from the caller and gives back the one the field held,
 * for the caller to release.
 */
static
PDEVICE_MAP
ObpSwapProcessDeviceMap(
    _In_ PEPROCESS Process,
    _In_opt_ PDEVICE_MAP DeviceMap)
{
    EX_FAST_REF OldValue;
    PDEVICE_MAP OldDeviceMap;
    ULONG Cached;

    if (DeviceMap != NULL) ObpReferenceDeviceMapBy(DeviceMap, MAX_FAST_REFS);

    PspLockProcessSecurityExclusive(Process);
    OldValue = ExSwapFastReference(&Process->DeviceMap, DeviceMap);
    PspUnlockProcessSecurityExclusive(Process);

    OldDeviceMap = ExGetObjectFastReference(OldValue);
    Cached = ExGetCountFastReference(OldValue);

    /* Give back the references nobody took out of the field */
    if (OldDeviceMap != NULL && Cached != 0)
        ObpReferenceDeviceMapBy(OldDeviceMap, -(LONG)Cached);

    return OldDeviceMap;
}

/* ATTACHING A MAP TO A DIRECTORY *********************************************/

/*
 * Gives the directory a map, or finds the one it already carries, and hands
 * the caller one reference on it.
 */
static
NTSTATUS
ObpAttachMapToDirectory(
    _In_ HANDLE DirectoryHandle,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN BecomeSystemMap,
    _In_ BOOLEAN AlwaysPermanent,
    _Outptr_ PDEVICE_MAP *Map)
{
    POBJECT_DIRECTORY Directory, GlobalDirectory;
    PDEVICE_MAP Fresh, Chosen;
    BOOLEAN Permanent;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(DirectoryHandle,
                                       DIRECTORY_TRAVERSE,
                                       ObpDirectoryObjectType,
                                       AccessMode,
                                       (PVOID *)&Directory,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Fresh = ExAllocatePoolWithTag(PagedPool, sizeof(*Fresh), OBP_DEVICE_MAP_TAG);
    if (Fresh == NULL)
    {
        ObDereferenceObject(Directory);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Fresh, sizeof(*Fresh));
    Fresh->ReferenceCount = 1;
    Fresh->DosDevicesDirectory = Directory;

    KeAcquireGuardedMutex(&ObpDeviceMapLock);

    Chosen = Directory->DeviceMap;
    if (Chosen == NULL)
    {
        Directory->DeviceMap = Fresh;
        Chosen = Fresh;
        Fresh = NULL;
    }
    else
    {
        ObpReferenceDeviceMapBy(Chosen, 1);
    }

    if (BecomeSystemMap) ObSystemDeviceMap = Chosen;

    GlobalDirectory = (ObSystemDeviceMap != NULL) ?
                      ObSystemDeviceMap->DosDevicesDirectory : NULL;

    /* Everything that is not the system map reaches the global names via it */
    Permanent = AlwaysPermanent;
    if (GlobalDirectory != NULL && GlobalDirectory != Directory)
    {
        Chosen->GlobalDosDevicesDirectory = GlobalDirectory;
        Permanent = TRUE;
    }

    KeReleaseGuardedMutex(&ObpDeviceMapLock);

    /* The map outlives the handle it was set up through, and so must its
     * directory, whose only other anchor is that handle */
    if (Permanent) ObpSetPermanentObject(Directory, TRUE);

    /* The directory already had a map, so the one we built is surplus and the
     * reference the map would have owned goes back */
    if (Fresh != NULL)
    {
        ExFreePoolWithTag(Fresh, OBP_DEVICE_MAP_TAG);
        ObDereferenceObject(Directory);
    }

    *Map = Chosen;
    return STATUS_SUCCESS;
}

/* LUID MAPPED DEVICES ********************************************************/

static
NTSTATUS
ObpSetCurrentProcessDeviceMap(VOID)
{
    PEPROCESS Self = PsGetCurrentProcess();
    LUID SystemLuid = SYSTEM_LUID;
    PDEVICE_MAP Chosen, Previous;
    PACCESS_TOKEN Token;
    NTSTATUS Status;
    LUID LogonId;

    PAGED_CODE();

    Token = PsReferencePrimaryToken(Self);
    if (Token == NULL) return STATUS_NO_TOKEN;

    Status = SeQueryAuthenticationIdToken(Token, &LogonId);
    if (NT_SUCCESS(Status))
    {
        if (RtlEqualLuid(&LogonId, &SystemLuid))
            Chosen = ObSystemDeviceMap;
        else if (!NT_SUCCESS(SeGetLogonIdDeviceMap(&LogonId, &Chosen)))
            Chosen = NULL;

        if (Chosen == NULL)
        {
            Status = STATUS_OBJECT_PATH_INVALID;
        }
        else
        {
            ObpReferenceDeviceMapBy(Chosen, 1);
            Previous = ObpSwapProcessDeviceMap(Self, Chosen);
            if (Previous != NULL) ObfDereferenceDeviceMap(Previous);
        }
    }

    PsDereferencePrimaryToken(Token);
    return Status;
}

/*
 * While impersonating, the map that applies is the one of the logon session
 * being impersonated, not the one the process was started with.
 */
static
PDEVICE_MAP
ObpImpersonationDeviceMap(VOID)
{
    SECURITY_IMPERSONATION_LEVEL Level;
    BOOLEAN CopyOnOpen, EffectiveOnly;
    LUID SystemLuid = SYSTEM_LUID;
    PDEVICE_MAP Chosen = NULL;
    PACCESS_TOKEN Token;
    LUID LogonId;

    PAGED_CODE();

    if (!PsGetCurrentThread()->ActiveImpersonationInfo) return NULL;

    Token = PsReferenceImpersonationToken(PsGetCurrentThread(),
                                          &CopyOnOpen,
                                          &EffectiveOnly,
                                          &Level);
    if (Token == NULL) return NULL;

    if (NT_SUCCESS(SeQueryAuthenticationIdToken(Token, &LogonId)))
    {
        if (RtlEqualLuid(&LogonId, &SystemLuid))
            Chosen = ObSystemDeviceMap;
        else if (!NT_SUCCESS(SeGetLogonIdDeviceMap(&LogonId, &Chosen)))
            Chosen = NULL;

        if (Chosen != NULL) ObpReferenceDeviceMapBy(Chosen, 1);
    }

    PsDereferenceImpersonationToken(Token);
    return Chosen;
}

PDEVICE_MAP
NTAPI
ObpReferenceDeviceMap(VOID)
{
    PEPROCESS Self = PsGetCurrentProcess();
    PDEVICE_MAP Chosen;

    if (ObpLUIDDeviceMapsEnabled != 0)
    {
        Chosen = ObpImpersonationDeviceMap();
        if (Chosen != NULL) return Chosen;

        /* Make sure the process carries a map before falling back onto it */
        if (ExGetObjectFastReference(Self->DeviceMap) == NULL &&
            !NT_SUCCESS(ObpSetCurrentProcessDeviceMap()))
        {
            return NULL;
        }
    }

    return ObpReferenceProcessDeviceMap(Self);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ObSetDeviceMap(
    _In_opt_ PEPROCESS Process,
    _In_ HANDLE DirectoryHandle)
{
    PDEVICE_MAP Map, Previous;
    NTSTATUS Status;

    /* Without a process this establishes the system map on the current one */
    Status = ObpAttachMapToDirectory(DirectoryHandle,
                                     KeGetPreviousMode(),
                                     (Process == NULL),
                                     FALSE,
                                     &Map);
    if (!NT_SUCCESS(Status)) return Status;

    Previous = ObpSwapProcessDeviceMap((Process != NULL) ? Process :
                                       PsGetCurrentProcess(), Map);
    if (Previous != NULL) ObfDereferenceDeviceMap(Previous);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObSetDirectoryDeviceMap(
    _Outptr_ PDEVICE_MAP *DeviceMap,
    _In_ HANDLE DirectoryHandle)
{
    return ObpAttachMapToDirectory(DirectoryHandle, KernelMode, FALSE, TRUE,
                                   DeviceMap);
}

VOID
NTAPI
ObDereferenceDeviceMap(
    _In_ PEPROCESS Process)
{
    PDEVICE_MAP Previous = ObpSwapProcessDeviceMap(Process, NULL);

    if (Previous != NULL) ObfDereferenceDeviceMap(Previous);
}

VOID
FASTCALL
ObfDereferenceDeviceMap(
    _In_ PDEVICE_MAP DeviceMap)
{
    POBJECT_DIRECTORY Directory;
    LONG Seen = *(volatile LONG *)&DeviceMap->ReferenceCount;
    LONG Was;

    /* Anything but the last reference is given back without the lock, so a map
     * can only ever reach zero with the lock held */
    while (Seen > 1)
    {
        Was = InterlockedCompareExchange((PLONG)&DeviceMap->ReferenceCount,
                                         Seen - 1,
                                         Seen);
        if (Was == Seen) return;
        Seen = Was;
    }

    KeAcquireGuardedMutex(&ObpDeviceMapLock);

    if (InterlockedDecrement((PLONG)&DeviceMap->ReferenceCount) != 0)
    {
        KeReleaseGuardedMutex(&ObpDeviceMapLock);
        return;
    }

    Directory = DeviceMap->DosDevicesDirectory;
    Directory->DeviceMap = NULL;

    KeReleaseGuardedMutex(&ObpDeviceMapLock);

    /* The directory was kept alive for the map's sake and can go with it */
    ObMakeTemporaryObject(Directory);
    ObDereferenceObject(Directory);
    ExFreePoolWithTag(DeviceMap, OBP_DEVICE_MAP_TAG);
}

VOID
NTAPI
ObInheritDeviceMap(
    _In_opt_ PEPROCESS Parent,
    _In_ PEPROCESS Process)
{
    PDEVICE_MAP Inherited, Previous;

    if (Parent != NULL)
    {
        Inherited = ObpReferenceProcessDeviceMap(Parent);
    }
    else
    {
        Inherited = ObSystemDeviceMap;
        if (Inherited != NULL) ObpReferenceDeviceMapBy(Inherited, 1);
    }

    if (Inherited == NULL) return;

    Previous = ObpSwapProcessDeviceMap(Process, Inherited);
    if (Previous != NULL) ObfDereferenceDeviceMap(Previous);
}

ULONG
NTAPI
ObIsLUIDDeviceMapsEnabled(VOID)
{
    return ObpLUIDDeviceMapsEnabled;
}

/* Picks the map whose drive letters answer for the named process. */
static
PDEVICE_MAP
ObpMapForQuery(
    _In_opt_ PEPROCESS Process)
{
    if (Process == NULL || Process == PsGetCurrentProcess())
        return ObpReferenceDeviceMap();

    return ObpReferenceProcessDeviceMap(Process);
}

NTSTATUS
NTAPI
ObQueryDeviceMapInformation(
    _In_opt_ PEPROCESS Process,
    _Out_ PPROCESS_DEVICEMAP_INFORMATION DeviceMapInfo,
    _In_ ULONG Flags)
{
    PROCESS_DEVICEMAP_INFORMATION Answer;
    PDEVICE_MAP Chosen, Global;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Merge;
    ULONG Letter;
    ULONG Bit;

    PAGED_CODE();

    if ((Flags & ~PROCESS_LUID_DOSDEVICES_ONLY) != 0)
        return STATUS_INVALID_PARAMETER;

    Merge = ((Flags & PROCESS_LUID_DOSDEVICES_ONLY) == 0);

    Chosen = ObpMapForQuery(Process);
    if (Chosen == NULL) return STATUS_END_OF_FILE;

    Global = NULL;
    if (Chosen->GlobalDosDevicesDirectory != NULL)
        Global = Chosen->GlobalDosDevicesDirectory->DeviceMap;

    Answer.Query.DriveMap = Chosen->DriveMap;

    for (Letter = 0; Letter < RTL_NUMBER_OF(Answer.Query.DriveType); Letter++)
    {
        Bit = 1u << Letter;
        Answer.Query.DriveType[Letter] = Chosen->DriveType[Letter];

        if (!Merge || Global == NULL) continue;
        if ((Answer.Query.DriveMap & Bit) != 0) continue;
        if ((Global->DriveMap & Bit) == 0) continue;

        /* A letter this map does not define is answered from the global one,
         * which never lends out a network or a still undecided drive */
        if (Global->DriveType[Letter] == DOSDEVICE_DRIVE_REMOTE ||
            Global->DriveType[Letter] == DOSDEVICE_DRIVE_CALCULATE)
        {
            continue;
        }

        Answer.Query.DriveType[Letter] = Global->DriveType[Letter];
        Answer.Query.DriveMap |= Bit;
    }

    ObfDereferenceDeviceMap(Chosen);

    _SEH2_TRY
    {
        RtlCopyMemory(&DeviceMapInfo->Query,
                      &Answer.Query,
                      sizeof(Answer.Query));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* EOF */
