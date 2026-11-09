/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Desktop and window station object types the window manager fills
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE ExWindowStationObjectType = NULL;
POBJECT_TYPE ExDesktopObjectType = NULL;

/*
 * The kernel owns these two object types, but everything they mean lives in the
 * window manager. These hold what it gave us to call; until it has given us
 * anything, the methods do nothing and say yes.
 */
PKWIN32_SESSION_CALLOUT ExpDesktopObjectOpen = NULL;
PKWIN32_SESSION_CALLOUT ExpDesktopObjectOkToClose = NULL;
PKWIN32_SESSION_CALLOUT ExpDesktopObjectClose = NULL;
PKWIN32_SESSION_CALLOUT ExpDesktopObjectDelete = NULL;

PKWIN32_SESSION_CALLOUT ExpWindowStationObjectOpen = NULL;
PKWIN32_SESSION_CALLOUT ExpWindowStationObjectOkToClose = NULL;
PKWIN32_SESSION_CALLOUT ExpWindowStationObjectClose = NULL;
PKWIN32_SESSION_CALLOUT ExpWindowStationObjectDelete = NULL;
PKWIN32_SESSION_CALLOUT ExpWindowStationObjectParse = NULL;

GENERIC_MAPPING ExpWindowStationMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE,
    STANDARD_RIGHTS_REQUIRED | GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE
};

GENERIC_MAPPING ExpDesktopMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE,
    STANDARD_RIGHTS_REQUIRED | GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE
};

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Every method goes out to the window manager through here, in the session the
 * object belongs to, because the window manager keeps its state per session.
 */
NTSTATUS
NTAPI
ExpWin32SessionCallout(
    _In_opt_ PKWIN32_SESSION_CALLOUT Callout,
    _In_ PVOID Parameters)
{
    if (Callout == NULL)
    {
        return STATUS_SUCCESS;
    }

    /* TODO: the call is made in the session the caller is already in; it is not
       switched to the session that owns the object. */
    return Callout(Parameters);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ExpWindowStationOpen(
    _In_ OB_OPEN_REASON OpenReason,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _Inout_ PACCESS_MASK GrantedAccess,
    _In_ ULONG HandleCount)
{
    WIN32_OPENMETHOD_PARAMETERS Parameters;

    Parameters.OpenReason = OpenReason;
    Parameters.AccessMode = AccessMode;
    Parameters.Process = Process;
    Parameters.Object = Object;
    Parameters.GrantedAccess = GrantedAccess;
    Parameters.HandleCount = HandleCount;

    return ExpWin32SessionCallout(ExpWindowStationObjectOpen, &Parameters);
}

BOOLEAN
NTAPI
ExpWindowStationOkToClose(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    WIN32_OKAYTOCLOSEMETHOD_PARAMETERS Parameters;

    Parameters.Process = Process;
    Parameters.Object = Object;
    Parameters.Handle = Handle;
    Parameters.PreviousMode = PreviousMode;

    return NT_SUCCESS(ExpWin32SessionCallout(
                          ExpWindowStationObjectOkToClose,
                          &Parameters));
}

VOID
NTAPI
ExpWindowStationClose(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    WIN32_CLOSEMETHOD_PARAMETERS Parameters;

    Parameters.Process = Process;
    Parameters.Object = Object;
    Parameters.ProcessHandleCount = ProcessHandleCount;
    Parameters.SystemHandleCount = SystemHandleCount;

    ExpWin32SessionCallout(ExpWindowStationObjectClose, &Parameters);
}

VOID
NTAPI
ExpWinStaObjectDelete(
    _In_ PVOID Object)
{
    WIN32_DELETEMETHOD_PARAMETERS Parameters;

    Parameters.Object = Object;

    ExpWin32SessionCallout(ExpWindowStationObjectDelete, &Parameters);
}

NTSTATUS
NTAPI
ExpWinStaObjectParse(
    _In_ PVOID ParseObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING CompleteName,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Out_ PVOID *Object)
{
    WIN32_PARSEMETHOD_PARAMETERS Parameters;

    Parameters.ParseObject = ParseObject;
    Parameters.ObjectType = ObjectType;
    Parameters.AccessState = AccessState;
    Parameters.AccessMode = AccessMode;
    Parameters.Attributes = Attributes;
    Parameters.CompleteName = CompleteName;
    Parameters.RemainingName = RemainingName;
    Parameters.Context = Context;
    Parameters.SecurityQos = SecurityQos;
    Parameters.Object = Object;

    *Object = NULL;

    if (ExpWindowStationObjectParse == NULL)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    return ExpWin32SessionCallout(ExpWindowStationObjectParse, &Parameters);
}

NTSTATUS
NTAPI
ExpDesktopOpen(
    _In_ OB_OPEN_REASON OpenReason,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _Inout_ PACCESS_MASK GrantedAccess,
    _In_ ULONG HandleCount)
{
    WIN32_OPENMETHOD_PARAMETERS Parameters;

    Parameters.OpenReason = OpenReason;
    Parameters.AccessMode = AccessMode;
    Parameters.Process = Process;
    Parameters.Object = Object;
    Parameters.GrantedAccess = GrantedAccess;
    Parameters.HandleCount = HandleCount;

    return ExpWin32SessionCallout(ExpDesktopObjectOpen, &Parameters);
}

BOOLEAN
NTAPI
ExpDesktopOkToClose(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    WIN32_OKAYTOCLOSEMETHOD_PARAMETERS Parameters;

    Parameters.Process = Process;
    Parameters.Object = Object;
    Parameters.Handle = Handle;
    Parameters.PreviousMode = PreviousMode;

    return NT_SUCCESS(ExpWin32SessionCallout(ExpDesktopObjectOkToClose,
                                             &Parameters));
}

VOID
NTAPI
ExpDesktopClose(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    WIN32_CLOSEMETHOD_PARAMETERS Parameters;

    Parameters.Process = Process;
    Parameters.Object = Object;
    Parameters.ProcessHandleCount = ProcessHandleCount;
    Parameters.SystemHandleCount = SystemHandleCount;

    ExpWin32SessionCallout(ExpDesktopObjectClose, &Parameters);
}

VOID
NTAPI
ExpDesktopDelete(
    _In_ PVOID Object)
{
    WIN32_DELETEMETHOD_PARAMETERS Parameters;

    Parameters.Object = Object;

    ExpWin32SessionCallout(ExpDesktopObjectDelete, &Parameters);
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpWin32kInit(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name;

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.ValidAccessMask = STANDARD_RIGHTS_REQUIRED |
                                  GENERIC_READ | GENERIC_WRITE |
                                  GENERIC_EXECUTE;
    Initializer.GenericMapping = ExpWindowStationMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.SecurityRequired = TRUE;
    Initializer.OpenProcedure = ExpWindowStationOpen;
    Initializer.OkayToCloseProcedure = ExpWindowStationOkToClose;
    Initializer.CloseProcedure = ExpWindowStationClose;
    Initializer.DeleteProcedure = ExpWinStaObjectDelete;
    Initializer.ParseProcedure = ExpWinStaObjectParse;

    RtlInitUnicodeString(&Name, L"WindowStation");

    if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                       &Initializer,
                                       NULL,
                                       &ExWindowStationObjectType)))
    {
        return FALSE;
    }

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.ValidAccessMask = STANDARD_RIGHTS_REQUIRED |
                                  GENERIC_READ | GENERIC_WRITE |
                                  GENERIC_EXECUTE;
    Initializer.GenericMapping = ExpDesktopMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.SecurityRequired = TRUE;
    Initializer.OpenProcedure = ExpDesktopOpen;
    Initializer.OkayToCloseProcedure = ExpDesktopOkToClose;
    Initializer.CloseProcedure = ExpDesktopClose;
    Initializer.DeleteProcedure = ExpDesktopDelete;

    RtlInitUnicodeString(&Name, L"Desktop");

    if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                       &Initializer,
                                       NULL,
                                       &ExDesktopObjectType)))
    {
        return FALSE;
    }

    return TRUE;
}

/* EOF */
