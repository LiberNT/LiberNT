/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * PURPOSE:           Rtl registry functions
 * FILE:              lib/rtl/registry.c
 * PROGRAMER:         Alex Ionescu (alex.ionescu@reactos.org)
 *                    Eric Kohl
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#include <ndk/cmfuncs.h>

#define NDEBUG
#include <debug.h>

/* DATA **********************************************************************/

PCWSTR RtlpRegPaths[RTL_REGISTRY_MAXIMUM] =
{
    NULL,
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Services",
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control",
    L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion",
    L"\\Registry\\Machine\\Hardware\\DeviceMap",
    L"\\Registry\\User\\.Default",
};

/* PRIVATE FUNCTIONS *********************************************************/

NTSTATUS
NTAPI
RtlpGetRegistryHandle(IN ULONG RelativeTo,
                      IN PCWSTR Path,
                      IN BOOLEAN Create,
                      IN PHANDLE KeyHandle)
{
    UNICODE_STRING KeyPath, KeyName;
    WCHAR KeyBuffer[MAX_PATH];
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    /* Check if we just want the handle */
    if (RelativeTo & RTL_REGISTRY_HANDLE)
    {
        *KeyHandle = (HANDLE)Path;
        return STATUS_SUCCESS;
    }

    /* Check for optional flag */
    if (RelativeTo & RTL_REGISTRY_OPTIONAL)
    {
        /* Mask it out */
        RelativeTo &= ~RTL_REGISTRY_OPTIONAL;
    }

    /* Fail on invalid parameter */
    if (RelativeTo >= RTL_REGISTRY_MAXIMUM) return STATUS_INVALID_PARAMETER;

    /* Initialize the key name */
    RtlInitEmptyUnicodeString(&KeyName, KeyBuffer, sizeof(KeyBuffer));

    /* Check if we have to lookup a path to prefix */
    if (RelativeTo != RTL_REGISTRY_ABSOLUTE)
    {
        /* Check if we need the current user key */
        if (RelativeTo == RTL_REGISTRY_USER)
        {
            /* Get the user key path */
            Status = RtlFormatCurrentUserKeyPath(&KeyPath);

            /* Check if it worked */
            if (NT_SUCCESS(Status))
            {
                /* Append the user key path */
                Status = RtlAppendUnicodeStringToString(&KeyName, &KeyPath);

                /* Free the user key path */
                RtlFreeUnicodeString (&KeyPath);
            }
            else
            {
                /* It didn't work so fall back to the default user key */
                Status = RtlAppendUnicodeToString(&KeyName, RtlpRegPaths[RTL_REGISTRY_USER]);
            }
        }
        else
        {
            /* Get one of the prefixes */
            Status = RtlAppendUnicodeToString(&KeyName,
                                              RtlpRegPaths[RelativeTo]);
        }

        /* Check for failure, otherwise, append the path separator */
        if (!NT_SUCCESS(Status)) return Status;
        Status = RtlAppendUnicodeToString(&KeyName, L"\\");
        if (!NT_SUCCESS(Status)) return Status;
    }

    if (Path != NULL)
    {
        /* And now append the path */
        if (Path[0] == L'\\' && RelativeTo != RTL_REGISTRY_ABSOLUTE) Path++; // HACK!
        Status = RtlAppendUnicodeToString(&KeyName, Path);
        if (!NT_SUCCESS(Status)) return Status;
    }

    /* Initialize the object attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    /* Check if we want to create it */
    if (Create)
    {
        /* Create the key with write privileges */
        Status = ZwCreateKey(KeyHandle,
                             GENERIC_WRITE,
                             &ObjectAttributes,
                             0,
                             NULL,
                             0,
                             NULL);
    }
    else
    {
        /* Otherwise, just open it with read access */
        Status = ZwOpenKey(KeyHandle,
                           MAXIMUM_ALLOWED | GENERIC_READ,
                           &ObjectAttributes);
    }

    /* Return status */
    return Status;
}

FORCEINLINE
VOID
RtlpCloseRegistryHandle(
    _In_ ULONG RelativeTo,
    _In_ HANDLE KeyHandle)
{
    /* Did the caller pass a key handle? */
    if (!(RelativeTo & RTL_REGISTRY_HANDLE))
    {
        /* We opened the key in RtlpGetRegistryHandle, so close it now */
        ZwClose(KeyHandle);
    }
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlCheckRegistryKey(IN ULONG RelativeTo,
                    IN PWSTR Path)
{
    HANDLE KeyHandle;
    NTSTATUS Status;
    PAGED_CODE_RTL();

    /* Call the helper */
    Status = RtlpGetRegistryHandle(RelativeTo,
                                   Path,
                                   FALSE,
                                   &KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    /* Close the handle even for RTL_REGISTRY_HANDLE */
    ZwClose(KeyHandle);
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlCreateRegistryKey(IN ULONG RelativeTo,
                     IN PWSTR Path)
{
    HANDLE KeyHandle;
    NTSTATUS Status;
    PAGED_CODE_RTL();

    /* Call the helper */
    Status = RtlpGetRegistryHandle(RelativeTo,
                                   Path,
                                   TRUE,
                                   &KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    /* All went well, close the handle and return status */
    RtlpCloseRegistryHandle(RelativeTo, KeyHandle);
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlDeleteRegistryValue(IN ULONG RelativeTo,
                       IN PCWSTR Path,
                       IN PCWSTR ValueName)
{
    HANDLE KeyHandle;
    NTSTATUS Status;
    UNICODE_STRING Name;
    PAGED_CODE_RTL();

    /* Call the helper */
    Status = RtlpGetRegistryHandle(RelativeTo,
                                   Path,
                                   TRUE,
                                   &KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    /* Initialize the key name and delete it */
    RtlInitUnicodeString(&Name, ValueName);
    Status = ZwDeleteValueKey(KeyHandle, &Name);

    /* Close the handle and return status */
    RtlpCloseRegistryHandle(RelativeTo, KeyHandle);
    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlWriteRegistryValue(IN ULONG RelativeTo,
                      IN PCWSTR Path,
                      IN PCWSTR ValueName,
                      IN ULONG ValueType,
                      IN PVOID ValueData,
                      IN ULONG ValueLength)
{
    HANDLE KeyHandle;
    NTSTATUS Status;
    UNICODE_STRING Name;
    PAGED_CODE_RTL();

    /* Call the helper */
    Status = RtlpGetRegistryHandle(RelativeTo,
                                   Path,
                                   TRUE,
                                   &KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    /* Initialize the key name and set it */
    RtlInitUnicodeString(&Name, ValueName);
    Status = ZwSetValueKey(KeyHandle,
                           &Name,
                           0,
                           ValueType,
                           ValueData,
                           ValueLength);

    /* Close the handle and return status */
    RtlpCloseRegistryHandle(RelativeTo, KeyHandle);
    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlOpenCurrentUser(IN ACCESS_MASK DesiredAccess,
                   OUT PHANDLE KeyHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyPath;
    NTSTATUS Status;
    PAGED_CODE_RTL();

    /* Get the user key */
    Status = RtlFormatCurrentUserKeyPath(&KeyPath);
    if (NT_SUCCESS(Status))
    {
        /* Initialize the attributes and open it */
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyPath,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);
        Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);

        /* Free the path and return success if it worked */
        RtlFreeUnicodeString(&KeyPath);
        if (NT_SUCCESS(Status)) return STATUS_SUCCESS;
    }

    /* It didn't work, so use the default key */
    RtlInitUnicodeString(&KeyPath, RtlpRegPaths[RTL_REGISTRY_USER]);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);

    /* Return status */
    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlFormatCurrentUserKeyPath(OUT PUNICODE_STRING KeyPath)
{
    HANDLE TokenHandle;
    UCHAR Buffer[256];
    PSID_AND_ATTRIBUTES SidBuffer;
    ULONG Length;
    UNICODE_STRING SidString;
    NTSTATUS Status;
    PAGED_CODE_RTL();

    /* Open the thread token */
    Status = ZwOpenThreadTokenEx(NtCurrentThread(),
                                 TOKEN_QUERY,
                                 TRUE,
                                 OBJ_KERNEL_HANDLE,
                                 &TokenHandle);
    if (!NT_SUCCESS(Status))
    {
        /* We failed, is it because we don't have a thread token? */
        if (Status != STATUS_NO_TOKEN) return Status;

        /* It is, so use the process token */
        Status = ZwOpenProcessTokenEx(NtCurrentProcess(),
                                      TOKEN_QUERY,
                                      OBJ_KERNEL_HANDLE,
                                      &TokenHandle);
        if (!NT_SUCCESS(Status)) return Status;
    }

    /* Now query the token information */
    SidBuffer = (PSID_AND_ATTRIBUTES)Buffer;
    Status = ZwQueryInformationToken(TokenHandle,
                                     TokenUser,
                                     (PVOID)SidBuffer,
                                     sizeof(Buffer),
                                     &Length);

    /* Close the handle and handle failure */
    ZwClose(TokenHandle);
    if (!NT_SUCCESS(Status)) return Status;

    /* Convert the SID */
    Status = RtlConvertSidToUnicodeString(&SidString, SidBuffer[0].Sid, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    /* Add the length of the prefix */
    Length = SidString.Length + sizeof(L"\\REGISTRY\\USER\\");

    /* Initialize a string */
    RtlInitEmptyUnicodeString(KeyPath,
                              RtlpAllocateStringMemory(Length, TAG_USTR),
                              (USHORT)Length);
    if (!KeyPath->Buffer)
    {
        /* Free the string and fail */
        RtlFreeUnicodeString(&SidString);
        return STATUS_NO_MEMORY;
    }

    /* Append the prefix and SID */
    RtlAppendUnicodeToString(KeyPath, L"\\REGISTRY\\USER\\");
    RtlAppendUnicodeStringToString(KeyPath, &SidString);

    /* Free the temporary string and return success */
    RtlFreeUnicodeString(&SidString);
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlpNtCreateKey(OUT HANDLE KeyHandle,
                IN ACCESS_MASK DesiredAccess,
                IN POBJECT_ATTRIBUTES ObjectAttributes,
                IN ULONG TitleIndex,
                IN PUNICODE_STRING Class,
                OUT PULONG Disposition)
{
    /* Check if we have object attributes */
    if (ObjectAttributes)
    {
        /* Mask out the unsupported flags */
        ObjectAttributes->Attributes &= ~(OBJ_PERMANENT | OBJ_EXCLUSIVE);
    }

    /* Create the key */
    return ZwCreateKey(KeyHandle,
                       DesiredAccess,
                       ObjectAttributes,
                       0,
                       NULL,
                       0,
                       Disposition);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlpNtEnumerateSubKey(IN HANDLE KeyHandle,
                      OUT PUNICODE_STRING SubKeyName,
                      IN ULONG Index,
                      IN ULONG Unused)
{
    PKEY_BASIC_INFORMATION KeyInfo = NULL;
    ULONG BufferLength = 0;
    ULONG ReturnedLength;
    NTSTATUS Status;

    /* Check if we have a name */
    if (SubKeyName->MaximumLength)
    {
        /* Allocate a buffer for it */
        BufferLength = SubKeyName->MaximumLength +
                       sizeof(KEY_BASIC_INFORMATION);
        KeyInfo = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
        if (!KeyInfo) return STATUS_NO_MEMORY;
    }

    /* Enumerate the key */
    Status = ZwEnumerateKey(KeyHandle,
                            Index,
                            KeyBasicInformation,
                            KeyInfo,
                            BufferLength,
                            &ReturnedLength);
    if (NT_SUCCESS(Status) && (KeyInfo != NULL))
    {
        /* Check if the name fits */
        if (KeyInfo->NameLength <= SubKeyName->MaximumLength)
        {
            /* Set the length */
            SubKeyName->Length = (USHORT)KeyInfo->NameLength;

            /* Copy it */
            RtlMoveMemory(SubKeyName->Buffer,
                          KeyInfo->Name,
                          SubKeyName->Length);
        }
        else
        {
            /* Otherwise, we ran out of buffer space */
            Status = STATUS_BUFFER_OVERFLOW;
        }
    }

    /* Free the buffer and return status */
    if (KeyInfo) RtlFreeHeap(RtlGetProcessHeap(), 0, KeyInfo);
    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlpNtMakeTemporaryKey(IN HANDLE KeyHandle)
{
    /* This just deletes the key */
    return ZwDeleteKey(KeyHandle);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlpNtOpenKey(OUT HANDLE KeyHandle,
              IN ACCESS_MASK DesiredAccess,
              IN POBJECT_ATTRIBUTES ObjectAttributes,
              IN ULONG Unused)
{
    /* Check if we have object attributes */
    if (ObjectAttributes)
    {
        /* Mask out the unsupported flags */
        ObjectAttributes->Attributes &= ~(OBJ_PERMANENT | OBJ_EXCLUSIVE);
    }

    /* Open the key */
    return ZwOpenKey(KeyHandle, DesiredAccess, ObjectAttributes);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlpNtQueryValueKey(IN HANDLE KeyHandle,
                    OUT PULONG Type OPTIONAL,
                    OUT PVOID Data OPTIONAL,
                    IN OUT PULONG DataLength OPTIONAL,
                    IN ULONG Unused)
{
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo;
    UNICODE_STRING ValueName;
    ULONG BufferLength = 0;
    NTSTATUS Status;

    /* Clear the value name */
    RtlInitEmptyUnicodeString(&ValueName, NULL, 0);

    /* Check if we were already given a length */
    if (DataLength) BufferLength = *DataLength;

    /* Add the size of the structure */
    BufferLength += FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);

    /* Allocate memory for the value */
    ValueInfo = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!ValueInfo) return STATUS_NO_MEMORY;

    /* Query the value */
    Status = ZwQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             ValueInfo,
                             BufferLength,
                             &BufferLength);
    if ((NT_SUCCESS(Status)) || (Status == STATUS_BUFFER_OVERFLOW))
    {
        /* Return the length and type */
        if (DataLength) *DataLength = ValueInfo->DataLength;
        if (Type) *Type = ValueInfo->Type;
    }

    /* Check if the caller wanted data back, and we got it */
    if ((NT_SUCCESS(Status)) && (Data))
    {
        /* Copy it */
        RtlMoveMemory(Data, ValueInfo->Data, ValueInfo->DataLength);
    }

    /* Free the memory and return status */
    RtlFreeHeap(RtlGetProcessHeap(), 0, ValueInfo);
    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlpNtSetValueKey(IN HANDLE KeyHandle,
                  IN ULONG Type,
                  IN PVOID Data,
                  IN ULONG DataLength)
{
    UNICODE_STRING ValueName;

    /* Set the value */
    RtlInitEmptyUnicodeString(&ValueName, NULL, 0);
    return ZwSetValueKey(KeyHandle,
                         &ValueName,
                         0,
                         Type,
                         Data,
                         DataLength);
}

NTSTATUS
NTAPI
RtlCheckPortableOperatingSystem(
    _Out_ PBOOLEAN IsPortable)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];
    ULONG Portable = 0;
    NTSTATUS Status;

    if (NT_SUCCESS(RtlCheckRegistryKey(RTL_REGISTRY_CONTROL, L"MiniNT")))
    {
        *IsPortable = FALSE;
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(QueryTable, sizeof(QueryTable));
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_TYPECHECK;
    QueryTable[0].Name = L"PortableOperatingSystem";
    QueryTable[0].EntryContext = &Portable;
    QueryTable[0].DefaultType = REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT;
    Status = RtlQueryRegistryValuesEx(RTL_REGISTRY_CONTROL, NULL, QueryTable, NULL, NULL);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        return STATUS_NOT_FOUND;
    if (NT_SUCCESS(Status))
        *IsPortable = Portable != 0;
    return Status;
}

NTSTATUS
NTAPI
RtlSetPortableOperatingSystem(
    _In_ BOOLEAN IsPortable)
{
    ULONG Portable = IsPortable ? 1 : 0;

    return RtlWriteRegistryValue(RTL_REGISTRY_CONTROL,
                                 NULL,
                                 L"PortableOperatingSystem",
                                 REG_DWORD,
                                 &Portable,
                                 sizeof(Portable));
}

/* EOF */
