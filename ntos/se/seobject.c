/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What an object's security is answered with when its type says
 *              nothing of its own
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* What has to be held over an object before a part of its security is read */
typedef struct _SEP_SECURITY_RIGHT
{
    SECURITY_INFORMATION Part;
    ACCESS_MASK ToRead;
    ACCESS_MASK ToWrite;
} SEP_SECURITY_RIGHT;

static const SEP_SECURITY_RIGHT SepSecurityRights[] =
{
    {OWNER_SECURITY_INFORMATION, READ_CONTROL, WRITE_OWNER},
    {GROUP_SECURITY_INFORMATION, READ_CONTROL, WRITE_OWNER},
    {LABEL_SECURITY_INFORMATION, READ_CONTROL, WRITE_OWNER},
    {DACL_SECURITY_INFORMATION, READ_CONTROL, WRITE_DAC},
    {ATTRIBUTE_SECURITY_INFORMATION, READ_CONTROL, WRITE_DAC},
    {SCOPE_SECURITY_INFORMATION, READ_CONTROL, WRITE_DAC},
    {PROCESS_TRUST_LABEL_SECURITY_INFORMATION, READ_CONTROL, WRITE_DAC},
    {ACCESS_FILTER_SECURITY_INFORMATION, READ_CONTROL, WRITE_DAC},
    {SACL_SECURITY_INFORMATION, ACCESS_SYSTEM_SECURITY, ACCESS_SYSTEM_SECURITY},
    /* TODO: BACKUP_SECURITY_INFORMATION is not declared by the headers here,
     * so asking for everything at once is not answered for */
};

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
SeQuerySecurityAccessMask(
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Out_ PACCESS_MASK DesiredAccess)
{
    ULONG i;

    *DesiredAccess = 0;

    for (i = 0; i < RTL_NUMBER_OF(SepSecurityRights); i++)
    {
        if (SecurityInformation & SepSecurityRights[i].Part)
            *DesiredAccess |= SepSecurityRights[i].ToRead;
    }
}

VOID
NTAPI
SeSetSecurityAccessMask(
    _In_ SECURITY_INFORMATION SecurityInformation,
    _Out_ PACCESS_MASK DesiredAccess)
{
    ULONG i;

    *DesiredAccess = 0;

    for (i = 0; i < RTL_NUMBER_OF(SepSecurityRights); i++)
    {
        if (SecurityInformation & SepSecurityRights[i].Part)
            *DesiredAccess |= SepSecurityRights[i].ToWrite;
    }
}

/*
 * The answer for every object whose type keeps its security in the object
 * manager's own field instead of answering for it itself.
 */
NTSTATUS
NTAPI
SeDefaultObjectMethod(
    _In_ PVOID Object,
    _In_ SECURITY_OPERATION_CODE OperationType,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _Inout_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_opt_ PULONG ReturnLength,
    _Inout_opt_ PSECURITY_DESCRIPTOR *OldSecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(AccessMode);

    switch (OperationType)
    {
        case SetSecurityDescriptor:
            return ObSetSecurityDescriptorInfo(Object,
                                               SecurityInformation,
                                               SecurityDescriptor,
                                               OldSecurityDescriptor,
                                               PoolType,
                                               GenericMapping);

        case QuerySecurityDescriptor:
            return ObQuerySecurityDescriptorInfo(Object,
                                                 SecurityInformation,
                                                 SecurityDescriptor,
                                                 ReturnLength,
                                                 OldSecurityDescriptor);

        case AssignSecurityDescriptor:
            return ObAssignObjectSecurityDescriptor(Object,
                                                    SecurityDescriptor,
                                                    PoolType);

        case DeleteSecurityDescriptor:
            return ObDeassignSecurity(OldSecurityDescriptor);

        default:
            return STATUS_INVALID_PARAMETER;
    }
}
