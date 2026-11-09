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

#define TAG_OB_DEVICE_MAP 'mDbO'

ULONG ObpLUIDDeviceMapsDisabled;
ULONG ObpLUIDDeviceMapsEnabled;

/* PRIVATE FUNCTIONS **********************************************************/

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
    ULONG_PTR Count;

    OldValue = ExAcquireFastReference(&Process->DeviceMap);
    DeviceMap = ExGetObjectFastReference(OldValue);
    Count = ExGetCountFastReference(OldValue);
    if (DeviceMap == NULL || Count > 1)
        return DeviceMap;

    if (Count == 1)
    {
        ObpReferenceDeviceMapBy(DeviceMap, MAX_FAST_REFS);
        if (!ExInsertFastReference(&Process->DeviceMap, DeviceMap))
            ObpReferenceDeviceMapBy(DeviceMap, -MAX_FAST_REFS);
        return DeviceMap;
    }

    PspLockProcessSecurityShared(Process);
    DeviceMap = ExGetObjectFastReference(Process->DeviceMap);
    if (DeviceMap != NULL)
        ObpReferenceDeviceMapBy(DeviceMap, 1);
    PspUnlockProcessSecurityShared(Process);
    return DeviceMap;
}

static
PDEVICE_MAP
ObpSwapProcessDeviceMap(
    _In_ PEPROCESS Process,
    _In_opt_ PDEVICE_MAP DeviceMap)
{
    EX_FAST_REF OldValue;
    PDEVICE_MAP OldDeviceMap;
    ULONG_PTR Count;

    if (DeviceMap != NULL)
        ObpReferenceDeviceMapBy(DeviceMap, MAX_FAST_REFS);

    PspLockProcessSecurityExclusive(Process);
    OldValue = ExSwapFastReference(&Process->DeviceMap, DeviceMap);
    PspUnlockProcessSecurityExclusive(Process);

    OldDeviceMap = ExGetObjectFastReference(OldValue);
    Count = ExGetCountFastReference(OldValue);
    if (OldDeviceMap != NULL && Count != 0)
        ObpReferenceDeviceMapBy(OldDeviceMap, -(LONG)Count);

    return OldDeviceMap;
}

/*
 * A directory that backs a device map other than the system one must survive
 * its last handle, so it is marked permanent once it is named.
 */
static
VOID
ObpMakeDeviceMapDirectoryPermanent(
    _In_ POBJECT_DIRECTORY DirectoryObject)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(DirectoryObject);
    POBJECT_HEADER_NAME_INFO HeaderNameInfo;
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);

    HeaderNameInfo = ObpReferenceNameInfo(ObjectHeader);

    ObpEnterObjectTypeMutex(ObjectType);
    if (HeaderNameInfo != NULL && HeaderNameInfo->Directory != NULL)
        ObjectHeader->Flags |= OB_FLAG_PERMANENT;
    ObpLeaveObjectTypeMutex(ObjectType);

    if (HeaderNameInfo != NULL) ObpDereferenceNameInfo(HeaderNameInfo);
}

/*
 * Builds the device map for a directory, or finds the one that is already
 * attached to it. Returns the map with one reference taken for the caller, and
 * tells the caller whether the directory has to be made permanent.
 */
static
NTSTATUS
ObpCreateDirectoryDeviceMap(
    _In_ HANDLE DirectoryHandle,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN PublishAsSystemMap,
    _Out_ PDEVICE_MAP *FoundDeviceMap,
    _Out_ POBJECT_DIRECTORY *FoundDirectory,
    _Out_ PBOOLEAN MakePermanent)
{
    POBJECT_DIRECTORY DirectoryObject;
    PDEVICE_MAP DeviceMap, Allocated;
    NTSTATUS Status;

    *MakePermanent = FALSE;

    Status = ObReferenceObjectByHandle(DirectoryHandle,
                                       DIRECTORY_TRAVERSE,
                                       ObpDirectoryObjectType,
                                       AccessMode,
                                       (PVOID *)&DirectoryObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Allocated = ExAllocatePoolWithTag(PagedPool,
                                      sizeof(*Allocated),
                                      TAG_OB_DEVICE_MAP);
    if (Allocated == NULL)
    {
        ObDereferenceObject(DirectoryObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Allocated, sizeof(*Allocated));
    Allocated->ReferenceCount = 1;
    Allocated->DosDevicesDirectory = DirectoryObject;

    KeAcquireGuardedMutex(&ObpDeviceMapLock);

    if (DirectoryObject->DeviceMap == NULL)
    {
        DirectoryObject->DeviceMap = Allocated;
        DeviceMap = Allocated;
        Allocated = NULL;
    }
    else
    {
        DeviceMap = DirectoryObject->DeviceMap;
        ObpReferenceDeviceMapBy(DeviceMap, 1);
    }

    if (PublishAsSystemMap) ObSystemDeviceMap = DeviceMap;

    /* Anything that is not the system map reaches the global names through it */
    if (DirectoryObject != ObSystemDeviceMap->DosDevicesDirectory)
    {
        DeviceMap->GlobalDosDevicesDirectory = ObSystemDeviceMap->DosDevicesDirectory;
        *MakePermanent = TRUE;
    }

    KeReleaseGuardedMutex(&ObpDeviceMapLock);

    /* The directory already had a map, so our own copy is dead weight */
    if (Allocated != NULL)
    {
        ObDereferenceObject(DirectoryObject);
        ExFreePoolWithTag(Allocated, TAG_OB_DEVICE_MAP);
    }

    *FoundDeviceMap = DeviceMap;
    *FoundDirectory = DirectoryObject;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObpSetCurrentProcessDeviceMap(VOID)
{
    PEPROCESS CurrentProcess = PsGetCurrentProcess();
    PDEVICE_MAP DeviceMap, OldDeviceMap;
    LUID SystemLuid = SYSTEM_LUID;
    NTSTATUS Status;
    LUID LogonId;
    PTOKEN Token;

    Token = PsReferencePrimaryToken(CurrentProcess);
    if (Token == NULL) return STATUS_NO_TOKEN;

    Status = SeQueryAuthenticationIdToken(Token, &LogonId);
    if (NT_SUCCESS(Status))
    {
        if (RtlEqualLuid(&LogonId, &SystemLuid))
        {
            DeviceMap = ObSystemDeviceMap;
        }
        else
        {
            Status = SeGetLogonIdDeviceMap(&LogonId, &DeviceMap);
            if (!NT_SUCCESS(Status)) Status = STATUS_OBJECT_PATH_INVALID;
        }

        if (NT_SUCCESS(Status) && DeviceMap == NULL)
            Status = STATUS_OBJECT_PATH_INVALID;

        if (NT_SUCCESS(Status))
        {
            ObpReferenceDeviceMapBy(DeviceMap, 1);
            OldDeviceMap = ObpSwapProcessDeviceMap(CurrentProcess, DeviceMap);
            if (OldDeviceMap != NULL) ObfDereferenceDeviceMap(OldDeviceMap);
        }
    }

    ObDereferenceObject(Token);
    return Status;
}

/*
 * With LUID device maps on, an impersonating thread looks up the map of the
 * logon session it impersonates rather than the one of its process.
 */
static
PDEVICE_MAP
ObpReferenceImpersonatedDeviceMap(
    _Out_ PBOOLEAN LookingForSystem,
    _Out_ PTOKEN *ImpersonationToken)
{
    SECURITY_IMPERSONATION_LEVEL ImpersonationLevel;
    BOOLEAN CopyOnOpen, EffectiveOnly;
    LUID SystemLuid = SYSTEM_LUID;
    PDEVICE_MAP DeviceMap;
    NTSTATUS Status;
    PTOKEN Token;
    LUID LogonId;

    *LookingForSystem = FALSE;
    *ImpersonationToken = NULL;

    if (!PsGetCurrentThread()->ActiveImpersonationInfo) return NULL;

    Token = PsReferenceImpersonationToken(PsGetCurrentThread(),
                                          &CopyOnOpen,
                                          &EffectiveOnly,
                                          &ImpersonationLevel);
    *ImpersonationToken = Token;
    if (Token == NULL) return NULL;

    if (!NT_SUCCESS(SeQueryAuthenticationIdToken(Token, &LogonId))) return NULL;

    if (RtlEqualLuid(&LogonId, &SystemLuid))
    {
        *LookingForSystem = TRUE;
        return NULL;
    }

    Status = SeGetLogonIdDeviceMap(&LogonId, &DeviceMap);
    if (!NT_SUCCESS(Status) || DeviceMap == NULL) return NULL;

    ObpReferenceDeviceMapBy(DeviceMap, 1);
    return DeviceMap;
}

PDEVICE_MAP
NTAPI
ObpReferenceDeviceMap(VOID)
{
    BOOLEAN LookingForSystem = FALSE;
    PTOKEN Token = NULL;
    PDEVICE_MAP DeviceMap;

    if (ObpLUIDDeviceMapsEnabled != 0)
    {
        DeviceMap = ObpReferenceImpersonatedDeviceMap(&LookingForSystem, &Token);
        if (DeviceMap != NULL)
        {
            if (Token != NULL) ObDereferenceObject(Token);
            return DeviceMap;
        }

        /* Make sure the process itself carries a map before falling back */
        if (ExGetObjectFastReference(PsGetCurrentProcess()->DeviceMap) == NULL &&
            !NT_SUCCESS(ObpSetCurrentProcessDeviceMap()))
        {
            if (Token != NULL) ObDereferenceObject(Token);
            return NULL;
        }
    }

    if (LookingForSystem)
    {
        DeviceMap = ObSystemDeviceMap;
        if (DeviceMap != NULL) ObpReferenceDeviceMapBy(DeviceMap, 1);
    }
    else
    {
        DeviceMap = ObpReferenceProcessDeviceMap(PsGetCurrentProcess());
    }

    if (Token != NULL) ObDereferenceObject(Token);
    return DeviceMap;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ObSetDeviceMap(
    _In_opt_ PEPROCESS Process,
    _In_ HANDLE DirectoryHandle)
{
    POBJECT_DIRECTORY DirectoryObject;
    PDEVICE_MAP DeviceMap, OldDeviceMap;
    BOOLEAN MakePermanent;
    PEPROCESS WorkProcess;
    NTSTATUS Status;

    /* Without a process this call establishes the system device map */
    Status = ObpCreateDirectoryDeviceMap(DirectoryHandle,
                                         KeGetPreviousMode(),
                                         (Process == NULL),
                                         &DeviceMap,
                                         &DirectoryObject,
                                         &MakePermanent);
    if (!NT_SUCCESS(Status)) return Status;

    WorkProcess = (Process != NULL) ? Process : PsGetCurrentProcess();
    OldDeviceMap = ObpSwapProcessDeviceMap(WorkProcess, DeviceMap);

    if (MakePermanent) ObpMakeDeviceMapDirectoryPermanent(DirectoryObject);
    if (OldDeviceMap != NULL) ObfDereferenceDeviceMap(OldDeviceMap);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObSetDirectoryDeviceMap(
    _Out_opt_ PDEVICE_MAP *DeviceMap,
    _In_ HANDLE DirectoryHandle)
{
    POBJECT_DIRECTORY DirectoryObject;
    PDEVICE_MAP LocalMap;
    BOOLEAN MakePermanent;
    NTSTATUS Status;

    Status = ObpCreateDirectoryDeviceMap(DirectoryHandle,
                                         KernelMode,
                                         FALSE,
                                         &LocalMap,
                                         &DirectoryObject,
                                         &MakePermanent);
    if (!NT_SUCCESS(Status)) return Status;

    if (DeviceMap != NULL) *DeviceMap = LocalMap;

    /* The caller keeps the map, so the directory always outlives its handle */
    ObpMakeDeviceMapDirectoryPermanent(DirectoryObject);

    return STATUS_SUCCESS;
}

VOID
NTAPI
ObDereferenceDeviceMap(
    _In_ PEPROCESS Process)
{
    PDEVICE_MAP DeviceMap = ObpSwapProcessDeviceMap(Process, NULL);

    if (DeviceMap != NULL) ObfDereferenceDeviceMap(DeviceMap);
}

VOID
FASTCALL
ObfDereferenceDeviceMap(
    _In_ PDEVICE_MAP DeviceMap)
{
    POBJECT_DIRECTORY DirectoryObject;
    LONG Count = *(volatile LONG *)&DeviceMap->ReferenceCount;

    /* Every reference but the last one goes away without the lock */
    while (Count > 1)
    {
        LONG Previous = InterlockedCompareExchange((PLONG)&DeviceMap->ReferenceCount,
                                                   Count - 1,
                                                   Count);
        if (Previous == Count) return;
        Count = Previous;
    }

    KeAcquireGuardedMutex(&ObpDeviceMapLock);

    if (InterlockedDecrement((PLONG)&DeviceMap->ReferenceCount) != 0)
    {
        KeReleaseGuardedMutex(&ObpDeviceMapLock);
        return;
    }

    DirectoryObject = DeviceMap->DosDevicesDirectory;
    DirectoryObject->DeviceMap = NULL;

    KeReleaseGuardedMutex(&ObpDeviceMapLock);

    ObMakeTemporaryObject(DirectoryObject);
    ObDereferenceObject(DirectoryObject);
    ExFreePoolWithTag(DeviceMap, TAG_OB_DEVICE_MAP);
}

VOID
NTAPI
ObInheritDeviceMap(
    _In_opt_ PEPROCESS Parent,
    _In_ PEPROCESS Process)
{
    PDEVICE_MAP DeviceMap;

    if (Parent != NULL)
    {
        DeviceMap = ObpReferenceProcessDeviceMap(Parent);
    }
    else
    {
        DeviceMap = ObSystemDeviceMap;
        if (DeviceMap != NULL) ObpReferenceDeviceMapBy(DeviceMap, 1);
    }

    if (DeviceMap != NULL)
    {
        DeviceMap = ObpSwapProcessDeviceMap(Process, DeviceMap);
        ASSERT(DeviceMap == NULL);
    }
}

NTSTATUS
NTAPI
ObQueryDeviceMapInformation(
    _In_opt_ PEPROCESS Process,
    _Out_ PPROCESS_DEVICEMAP_INFORMATION DeviceMapInfo,
    _In_ ULONG Flags)
{
    PDEVICE_MAP DeviceMap = NULL, GlobalDeviceMap;
    PROCESS_DEVICEMAP_INFORMATION MapInfo;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN ReturnAny;
    ULONG BitMask;
    ULONG Index;

    if (Flags & ~PROCESS_LUID_DOSDEVICES_ONLY) return STATUS_INVALID_PARAMETER;

    ReturnAny = ((Flags & PROCESS_LUID_DOSDEVICES_ONLY) == 0);

    if (ObpLUIDDeviceMapsEnabled != 0)
    {
        /* A LUID map only ever describes the caller's own process */
        if (Process != NULL && Process != PsGetCurrentProcess())
            return STATUS_INVALID_PARAMETER;

        DeviceMap = ObpReferenceDeviceMap();
    }

    if (DeviceMap == NULL)
    {
        if (Process != NULL)
        {
            DeviceMap = ObpReferenceProcessDeviceMap(Process);
        }
        else
        {
            DeviceMap = ObSystemDeviceMap;
            if (DeviceMap != NULL) ObpReferenceDeviceMapBy(DeviceMap, 1);
        }
    }

    if (DeviceMap == NULL) return STATUS_END_OF_FILE;

    GlobalDeviceMap = DeviceMap;
    if (DeviceMap->GlobalDosDevicesDirectory != NULL &&
        DeviceMap->GlobalDosDevicesDirectory->DeviceMap != NULL)
    {
        GlobalDeviceMap = DeviceMap->GlobalDosDevicesDirectory->DeviceMap;
    }

    MapInfo.Query.DriveMap = DeviceMap->DriveMap;
    for (Index = 0, BitMask = 1; Index < 32; ++Index, BitMask <<= 1)
    {
        MapInfo.Query.DriveType[Index] = DeviceMap->DriveType[Index];

        /* A letter the caller's map does not define is answered from the
         * global map, which never lends out a network or deferred drive */
        if (!(MapInfo.Query.DriveMap & BitMask) && ReturnAny)
        {
            if (ObpLUIDDeviceMapsEnabled != 0 ||
                (GlobalDeviceMap->DriveType[Index] != DOSDEVICE_DRIVE_REMOTE &&
                 GlobalDeviceMap->DriveType[Index] != DOSDEVICE_DRIVE_CALCULATE))
            {
                MapInfo.Query.DriveType[Index] = GlobalDeviceMap->DriveType[Index];
                MapInfo.Query.DriveMap |= BitMask & GlobalDeviceMap->DriveMap;
            }
        }
    }

    ObfDereferenceDeviceMap(DeviceMap);

    _SEH2_TRY
    {
        RtlCopyMemory(&DeviceMapInfo->Query, &MapInfo.Query, sizeof(MapInfo.Query));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

ULONG
NTAPI
ObIsLUIDDeviceMapsEnabled(VOID)
{
    return ObpLUIDDeviceMapsEnabled;
}

/* EOF */
