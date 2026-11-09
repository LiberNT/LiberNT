/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a key is as an object the rest of the kernel can hold
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* GLOBALS ********************************************************************/

POBJECT_TYPE CmpKeyObjectType = NULL;
POBJECT_TYPE CmKeyObjectType = NULL;

/* PRIVATE DATA ***************************************************************/

static GENERIC_MAPPING CmpKeyMapping =
{
    STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS |
        KEY_NOTIFY,
    STANDARD_RIGHTS_WRITE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY,
    STANDARD_RIGHTS_EXECUTE | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS |
        KEY_NOTIFY,
    KEY_ALL_ACCESS
};

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpCreateObjectTypes(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name;
    NTSTATUS Status;

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.CaseInsensitive = TRUE;
    Initializer.UseDefaultObject = TRUE;
    Initializer.SecurityRequired = TRUE;
    Initializer.MaintainHandleCount = TRUE;
    Initializer.InvalidAttributes = OBJ_EXCLUSIVE | OBJ_PERMANENT;
    Initializer.GenericMapping = CmpKeyMapping;
    Initializer.ValidAccessMask = KEY_ALL_ACCESS;
    Initializer.PoolType = PagedPool;
    Initializer.DeleteProcedure = CmpDeleteKeyObject;
    Initializer.CloseProcedure = CmpCloseKeyObject;
    Initializer.ParseProcedure = CmpParseKey;
    Initializer.SecurityProcedure = CmpSecurityMethod;
    Initializer.QueryNameProcedure = CmpQueryKeyName;

    RtlInitUnicodeString(&Name, L"Key");

    Status = ObCreateObjectType(&Name,
                                &Initializer,
                                NULL,
                                &CmpKeyObjectType);
    if (!NT_SUCCESS(Status)) return Status;

    CmKeyObjectType = CmpKeyObjectType;
    return STATUS_SUCCESS;
}

/*
 * The last handle on a key is also the last chance a filter driver gets to
 * see it, and the point at which an outstanding change notification on that
 * handle has to be answered rather than left waiting forever.
 */
VOID
NTAPI
CmpCloseKeyObject(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    PCM_KEY_BODY KeyBody = Object;
    REG_KEY_HANDLE_CLOSE_INFORMATION Information;
    CMP_CALLBACK_CALL Call;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessHandleCount);

    if (KeyBody->Type != CM_KEY_BODY_TYPE) return;
    if (SystemHandleCount > 1) return;

    if (CmpCallBackCount != 0)
    {
        RtlZeroMemory(&Information, sizeof(Information));
        Information.Object = Object;

        CmpPreCallbacks(&Call,
                        RegNtPreKeyHandleClose,
                        RegNtPostKeyHandleClose,
                        &Information,
                        &Information.CallContext,
                        &Information.ObjectContext,
                        KeyBody,
                        NULL,
                        &Status);
        CmpPostCallbacks(&Call, Status);
    }

    if (KeyBody->NotifyBlock != NULL) CmpCloseNotify(KeyBody);
}

VOID
NTAPI
CmpDeleteKeyObject(
    _In_ PVOID Object)
{
    PCM_KEY_BODY KeyBody = Object;
    PCM_KEY_CONTROL_BLOCK Kcb;

    if (KeyBody->Type != CM_KEY_BODY_TYPE) return;

    CmpCleanupKeyBodyContexts(KeyBody);
    CmpTransUnbindKeyBody(KeyBody);

    Kcb = KeyBody->KeyControlBlock;
    if (Kcb == NULL) return;

    CmpLockRegistry();

    DelistKeyBodyFromKCB(KeyBody, FALSE);
    KeyBody->KeyControlBlock = NULL;
    CmpDereferenceKeyControlBlock(Kcb);

    CmpUnlockRegistry();
}

NTSTATUS
NTAPI
CmpQueryKeyName(
    _In_ PVOID Object,
    _In_ BOOLEAN HasObjectName,
    _Out_ POBJECT_NAME_INFORMATION ObjectNameInfo,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PCM_KEY_BODY KeyBody = Object;
    PUNICODE_STRING FullName;
    NTSTATUS Status;
    ULONG Required;

    UNREFERENCED_PARAMETER(HasObjectName);
    UNREFERENCED_PARAMETER(AccessMode);

    if (KeyBody->Type != CM_KEY_BODY_TYPE) return STATUS_OBJECT_TYPE_MISMATCH;
    if (KeyBody->KeyControlBlock == NULL) return STATUS_KEY_DELETED;

    CmpLockRegistry();

    FullName = CmpConstructName(KeyBody->KeyControlBlock);

    CmpUnlockRegistry();

    if (FullName == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Required = (ULONG)sizeof(OBJECT_NAME_INFORMATION) +
               FullName->Length + sizeof(WCHAR);
    *ReturnLength = Required;

    if (Length < Required)
    {
        ExFreePool(FullName);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    ObjectNameInfo->Name.Buffer = (PWCHAR)(ObjectNameInfo + 1);
    ObjectNameInfo->Name.Length = FullName->Length;
    ObjectNameInfo->Name.MaximumLength = FullName->Length + sizeof(WCHAR);

    RtlCopyMemory(ObjectNameInfo->Name.Buffer,
                  FullName->Buffer,
                  FullName->Length);
    ObjectNameInfo->Name.Buffer[FullName->Length / sizeof(WCHAR)] = UNICODE_NULL;

    ExFreePool(FullName);

    Status = STATUS_SUCCESS;
    return Status;
}
