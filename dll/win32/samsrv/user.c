/*
 * PROJECT:     Local Security Authority Server DLL
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        dll/win32/samsrv/user.c
 * PURPOSE:     User specific helper functions
 * COPYRIGHT:   Copyright 2013 Eric Kohl
 */

#include "samsrv.h"

#define SAM_PASSWORD_KEY_SIZE 16
#define SAM_PASSWORD_IV_SIZE 16
#define SAM_PASSWORD_ENCRYPTION_REVISION 1

static BCRYPT_ALG_HANDLE SampPasswordAlgorithm;
static BCRYPT_KEY_HANDLE SampPasswordKey;

/* FUNCTIONS ***************************************************************/

NTSTATUS
SampOpenUserObject(IN PSAM_DB_OBJECT DomainObject,
                   IN ULONG UserId,
                   IN ACCESS_MASK DesiredAccess,
                   OUT PSAM_DB_OBJECT *UserObject)
{
    WCHAR szRid[9];

    TRACE("(%p %lu %lx %p)\n",
          DomainObject, UserId, DesiredAccess, UserObject);

    /* Convert the RID into a string (hex) */
    _swprintf(szRid, L"%08lX", UserId);

    /* Create the user object */
    return SampOpenDbObject(DomainObject,
                            L"Users",
                            szRid,
                            UserId,
                            SamDbUserObject,
                            DesiredAccess,
                            UserObject);
}


NTSTATUS
SampAddGroupMembershipToUser(IN PSAM_DB_OBJECT UserObject,
                             IN ULONG GroupId,
                             IN ULONG Attributes)
{
    PGROUP_MEMBERSHIP GroupsBuffer = NULL;
    ULONG GroupsCount = 0;
    ULONG Length = 0;
    ULONG i;
    NTSTATUS Status;

    TRACE("(%p %lu %lx)\n",
          UserObject, GroupId, Attributes);

    Status = SampGetObjectAttribute(UserObject,
                                    L"Groups",
                                    NULL,
                                    NULL,
                                    &Length);
    if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_NOT_FOUND)
        goto done;

    GroupsBuffer = midl_user_allocate(Length + sizeof(GROUP_MEMBERSHIP));
    if (GroupsBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        Status = SampGetObjectAttribute(UserObject,
                                        L"Groups",
                                        NULL,
                                        GroupsBuffer,
                                        &Length);
        if (!NT_SUCCESS(Status))
            goto done;

        GroupsCount = Length / sizeof(GROUP_MEMBERSHIP);
    }

    for (i = 0; i < GroupsCount; i++)
    {
        if (GroupsBuffer[i].RelativeId == GroupId)
        {
            Status = STATUS_MEMBER_IN_GROUP;
            goto done;
        }
    }

    GroupsBuffer[GroupsCount].RelativeId = GroupId;
    GroupsBuffer[GroupsCount].Attributes = Attributes;
    Length += sizeof(GROUP_MEMBERSHIP);

    Status = SampSetObjectAttribute(UserObject,
                                    L"Groups",
                                    REG_BINARY,
                                    GroupsBuffer,
                                    Length);

done:
    if (GroupsBuffer != NULL)
        midl_user_free(GroupsBuffer);

    return Status;
}


NTSTATUS
SampRemoveGroupMembershipFromUser(IN PSAM_DB_OBJECT UserObject,
                                  IN ULONG GroupId)
{
    PGROUP_MEMBERSHIP GroupsBuffer = NULL;
    ULONG GroupsCount = 0;
    ULONG Length = 0;
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;

    TRACE("(%p %lu)\n",
          UserObject, GroupId);

    SampGetObjectAttribute(UserObject,
                           L"Groups",
                           NULL,
                           NULL,
                           &Length);

    if (Length == 0)
        return STATUS_MEMBER_NOT_IN_GROUP;

    GroupsBuffer = midl_user_allocate(Length);
    if (GroupsBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    Status = SampGetObjectAttribute(UserObject,
                                    L"Groups",
                                    NULL,
                                    GroupsBuffer,
                                    &Length);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = STATUS_MEMBER_NOT_IN_GROUP;

    GroupsCount = Length / sizeof(GROUP_MEMBERSHIP);
    for (i = 0; i < GroupsCount; i++)
    {
        if (GroupsBuffer[i].RelativeId == GroupId)
        {
            Length -= sizeof(GROUP_MEMBERSHIP);
            Status = STATUS_SUCCESS;

            if (GroupsCount - i - 1 > 0)
            {
                CopyMemory(&GroupsBuffer[i],
                           &GroupsBuffer[i + 1],
                           (GroupsCount - i - 1) * sizeof(GROUP_MEMBERSHIP));
            }

            break;
        }
    }

    if (!NT_SUCCESS(Status))
        goto done;

    Status = SampSetObjectAttribute(UserObject,
                                    L"Groups",
                                    REG_BINARY,
                                    GroupsBuffer,
                                    Length);

done:
    if (GroupsBuffer != NULL)
        midl_user_free(GroupsBuffer);

    return Status;
}


NTSTATUS
SampGetUserGroupAttributes(IN PSAM_DB_OBJECT DomainObject,
                           IN ULONG UserId,
                           IN ULONG GroupId,
                           OUT PULONG GroupAttributes)
{
    PSAM_DB_OBJECT UserObject = NULL;
    PGROUP_MEMBERSHIP GroupsBuffer = NULL;
    ULONG Length = 0;
    ULONG i;
    NTSTATUS Status;

    Status = SampOpenUserObject(DomainObject,
                                UserId,
                                0,
                                &UserObject);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    SampGetObjectAttribute(UserObject,
                           L"Groups",
                           NULL,
                           NULL,
                           &Length);

    if (Length == 0)
        return STATUS_UNSUCCESSFUL; /* FIXME */

    GroupsBuffer = midl_user_allocate(Length);
    if (GroupsBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    Status = SampGetObjectAttribute(UserObject,
                                    L"Groups",
                                    NULL,
                                    GroupsBuffer,
                                    &Length);
    if (!NT_SUCCESS(Status))
        goto done;

    for (i = 0; i < (Length / sizeof(GROUP_MEMBERSHIP)); i++)
    {
        if (GroupsBuffer[i].RelativeId == GroupId)
        {
            *GroupAttributes = GroupsBuffer[i].Attributes;
            goto done;
        }
    }

done:
    if (GroupsBuffer != NULL)
        midl_user_free(GroupsBuffer);

    if (UserObject != NULL)
        SampCloseDbObject(UserObject);

    return Status;
}


NTSTATUS
SampSetUserGroupAttributes(IN PSAM_DB_OBJECT DomainObject,
                           IN ULONG UserId,
                           IN ULONG GroupId,
                           IN ULONG GroupAttributes)
{
    PSAM_DB_OBJECT UserObject = NULL;
    PGROUP_MEMBERSHIP GroupsBuffer = NULL;
    ULONG Length = 0;
    ULONG i;
    NTSTATUS Status;

    Status = SampOpenUserObject(DomainObject,
                                UserId,
                                0,
                                &UserObject);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    SampGetObjectAttribute(UserObject,
                           L"Groups",
                           NULL,
                           NULL,
                           &Length);

    if (Length == 0)
        return STATUS_UNSUCCESSFUL; /* FIXME */

    GroupsBuffer = midl_user_allocate(Length);
    if (GroupsBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    Status = SampGetObjectAttribute(UserObject,
                                    L"Groups",
                                    NULL,
                                    GroupsBuffer,
                                    &Length);
    if (!NT_SUCCESS(Status))
        goto done;

    for (i = 0; i < (Length / sizeof(GROUP_MEMBERSHIP)); i++)
    {
        if (GroupsBuffer[i].RelativeId == GroupId)
        {
            GroupsBuffer[i].Attributes = GroupAttributes;
            break;
        }
    }

    Status = SampSetObjectAttribute(UserObject,
                                    L"Groups",
                                    REG_BINARY,
                                    GroupsBuffer,
                                    Length);

done:
    if (GroupsBuffer != NULL)
        midl_user_free(GroupsBuffer);

    if (UserObject != NULL)
        SampCloseDbObject(UserObject);

    return Status;
}


NTSTATUS
SampRemoveUserFromAllGroups(IN PSAM_DB_OBJECT UserObject)
{
    PGROUP_MEMBERSHIP GroupsBuffer = NULL;
    PSAM_DB_OBJECT GroupObject;
    ULONG Length = 0;
    ULONG i;
    NTSTATUS Status;

    SampGetObjectAttribute(UserObject,
                           L"Groups",
                           NULL,
                           NULL,
                           &Length);

    if (Length == 0)
        return STATUS_SUCCESS;

    GroupsBuffer = midl_user_allocate(Length);
    if (GroupsBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    Status = SampGetObjectAttribute(UserObject,
                                    L"Groups",
                                    NULL,
                                    GroupsBuffer,
                                    &Length);
    if (!NT_SUCCESS(Status))
        goto done;

    for (i = 0; i < (Length / sizeof(GROUP_MEMBERSHIP)); i++)
    {
        Status = SampOpenGroupObject(UserObject->ParentObject,
                                     GroupsBuffer[i].RelativeId,
                                     0,
                                     &GroupObject);
        if (!NT_SUCCESS(Status))
        {
            goto done;
        }

        Status = SampRemoveMemberFromGroup(GroupObject,
                                           UserObject->RelativeId);
        if (Status == STATUS_MEMBER_NOT_IN_GROUP)
            Status = STATUS_SUCCESS;

        SampCloseDbObject(GroupObject);

        if (!NT_SUCCESS(Status))
        {
            goto done;
        }
    }

    /* Remove all groups from the Groups attribute */
    Status = SampSetObjectAttribute(UserObject,
                                    L"Groups",
                                    REG_BINARY,
                                    NULL,
                                    0);

done:
    if (GroupsBuffer != NULL)
        midl_user_free(GroupsBuffer);

    return Status;
}


NTSTATUS
SampRemoveUserFromAllAliases(IN PSAM_DB_OBJECT UserObject)
{
    FIXME("(%p)\n", UserObject);
    return STATUS_SUCCESS;
}


NTSTATUS
SampInitPasswordEncryption(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Lsa\\SamKey");
    OBJECT_ATTRIBUTES ObjectAttributes;
    PSECURITY_DESCRIPTOR Sd = NULL;
    HANDLE KeyHandle = NULL;
    UCHAR KeyData[SAM_PASSWORD_KEY_SIZE];
    ULONG Length;
    ULONG Disposition;
    NTSTATUS Status;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;KA;;;SY)",
                                                              SDDL_REVISION_1,
                                                              &Sd,
                                                              NULL))
        return STATUS_INSUFFICIENT_RESOURCES;

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               Sd);

    Status = ZwCreateKey(&KeyHandle,
                         KEY_QUERY_VALUE | KEY_SET_VALUE,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         &Disposition);
    LocalFree(Sd);
    if (!NT_SUCCESS(Status))
    {
        ERR("ZwCreateKey failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    Length = sizeof(KeyData);
    Status = SampRegQueryValue(KeyHandle,
                               NULL,
                               NULL,
                               KeyData,
                               &Length);
    if (!NT_SUCCESS(Status) || Length != sizeof(KeyData))
    {
        Status = BCryptGenRandom(NULL,
                                 KeyData,
                                 sizeof(KeyData),
                                 BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (NT_SUCCESS(Status))
        {
            Status = SampRegSetValue(KeyHandle,
                                     NULL,
                                     REG_BINARY,
                                     KeyData,
                                     sizeof(KeyData));
        }
    }

    NtClose(KeyHandle);

    if (!NT_SUCCESS(Status))
    {
        ERR("Failed to get the password encryption key (Status 0x%08lx)\n", Status);
        goto done;
    }

    Status = BCryptOpenAlgorithmProvider(&SampPasswordAlgorithm,
                                         BCRYPT_AES_ALGORITHM,
                                         NULL,
                                         0);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = BCryptSetProperty(SampPasswordAlgorithm,
                               BCRYPT_CHAINING_MODE,
                               (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                               sizeof(BCRYPT_CHAIN_MODE_CBC),
                               0);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = BCryptGenerateSymmetricKey(SampPasswordAlgorithm,
                                        &SampPasswordKey,
                                        NULL,
                                        0,
                                        KeyData,
                                        sizeof(KeyData),
                                        0);

done:
    RtlSecureZeroMemory(KeyData, sizeof(KeyData));

    if (!NT_SUCCESS(Status))
        ERR("Password encryption initialization failed (Status 0x%08lx)\n", Status);

    return Status;
}


static
NTSTATUS
SampEncryptPasswordData(IN PVOID Data,
                        IN ULONG Length,
                        OUT PUCHAR *EncryptedData,
                        OUT PULONG EncryptedLength)
{
    UCHAR Iv[SAM_PASSWORD_IV_SIZE];
    PUCHAR Buffer;
    ULONG BufferLength;
    ULONG ResultLength;
    NTSTATUS Status;

    *EncryptedData = NULL;
    *EncryptedLength = 0;

    if ((SampPasswordKey == NULL) || (Length % SAM_PASSWORD_IV_SIZE) != 0)
        return STATUS_INTERNAL_ERROR;

    BufferLength = sizeof(ULONG) + SAM_PASSWORD_IV_SIZE + Length;
    Buffer = midl_user_allocate(BufferLength);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    *(PULONG)Buffer = SAM_PASSWORD_ENCRYPTION_REVISION;

    Status = BCryptGenRandom(NULL,
                             Buffer + sizeof(ULONG),
                             SAM_PASSWORD_IV_SIZE,
                             BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(Iv, Buffer + sizeof(ULONG), sizeof(Iv));
        Status = BCryptEncrypt(SampPasswordKey,
                               Data,
                               Length,
                               NULL,
                               Iv,
                               sizeof(Iv),
                               Buffer + sizeof(ULONG) + SAM_PASSWORD_IV_SIZE,
                               Length,
                               &ResultLength,
                               0);
    }

    if (!NT_SUCCESS(Status))
    {
        midl_user_free(Buffer);
        return Status;
    }

    *EncryptedData = Buffer;
    *EncryptedLength = BufferLength;

    return STATUS_SUCCESS;
}


static
NTSTATUS
SampGetPasswordAttribute(IN PSAM_DB_OBJECT UserObject,
                         IN LPWSTR AttributeName,
                         OUT PUCHAR *Data,
                         OUT PULONG Length)
{
    UCHAR Iv[SAM_PASSWORD_IV_SIZE];
    PUCHAR RawData = NULL;
    PUCHAR PlainData = NULL;
    ULONG RawLength = 0;
    ULONG PlainLength;
    ULONG ResultLength;
    NTSTATUS Status;

    *Data = NULL;
    *Length = 0;

    Status = SampGetObjectAttribute(UserObject,
                                    AttributeName,
                                    NULL,
                                    NULL,
                                    &RawLength);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;
    if (RawLength == 0)
        return STATUS_SUCCESS;

    RawData = midl_user_allocate(RawLength);
    if (RawData == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = SampGetObjectAttribute(UserObject,
                                    AttributeName,
                                    NULL,
                                    RawData,
                                    &RawLength);
    if (!NT_SUCCESS(Status))
        goto done;

    if ((RawLength % SAM_PASSWORD_IV_SIZE) == 0)
    {
        *Data = RawData;
        *Length = RawLength;
        RawData = NULL;
        goto done;
    }

    if ((RawLength < sizeof(ULONG) + 2 * SAM_PASSWORD_IV_SIZE) ||
        ((RawLength - sizeof(ULONG)) % SAM_PASSWORD_IV_SIZE) != 0 ||
        (*(PULONG)RawData != SAM_PASSWORD_ENCRYPTION_REVISION) ||
        (SampPasswordKey == NULL))
    {
        Status = STATUS_INTERNAL_DB_CORRUPTION;
        goto done;
    }

    PlainLength = RawLength - sizeof(ULONG) - SAM_PASSWORD_IV_SIZE;
    PlainData = midl_user_allocate(PlainLength);
    if (PlainData == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    RtlCopyMemory(Iv, RawData + sizeof(ULONG), sizeof(Iv));
    Status = BCryptDecrypt(SampPasswordKey,
                           RawData + sizeof(ULONG) + SAM_PASSWORD_IV_SIZE,
                           PlainLength,
                           NULL,
                           Iv,
                           sizeof(Iv),
                           PlainData,
                           PlainLength,
                           &ResultLength,
                           0);
    if (!NT_SUCCESS(Status))
    {
        RtlSecureZeroMemory(PlainData, PlainLength);
        midl_user_free(PlainData);
        goto done;
    }

    *Data = PlainData;
    *Length = PlainLength;

done:
    if (RawData != NULL)
    {
        RtlSecureZeroMemory(RawData, RawLength);
        midl_user_free(RawData);
    }

    return Status;
}


static
NTSTATUS
SampSetPasswordAttribute(IN PSAM_DB_OBJECT UserObject,
                         IN LPWSTR AttributeName,
                         IN PVOID Data,
                         IN ULONG Length)
{
    PUCHAR EncryptedData;
    ULONG EncryptedLength;
    NTSTATUS Status;

    Status = SampEncryptPasswordData(Data,
                                     Length,
                                     &EncryptedData,
                                     &EncryptedLength);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SampSetObjectAttribute(UserObject,
                                    AttributeName,
                                    REG_BINARY,
                                    EncryptedData,
                                    EncryptedLength);

    RtlSecureZeroMemory(EncryptedData, EncryptedLength);
    midl_user_free(EncryptedData);

    return Status;
}


NTSTATUS
SampGetUserNtPassword(IN PSAM_DB_OBJECT UserObject,
                      OUT PENCRYPTED_NT_OWF_PASSWORD NtPassword,
                      OUT PBOOLEAN NtPasswordPresent)
{
    PUCHAR Data;
    ULONG Length;
    NTSTATUS Status;

    *NtPassword = EmptyNtHash;
    *NtPasswordPresent = FALSE;

    Status = SampGetPasswordAttribute(UserObject,
                                      L"NTPwd",
                                      &Data,
                                      &Length);
    if (!NT_SUCCESS(Status))
    {
        ERR("Failed to read the NT password of user %lu (Status 0x%08lx)\n",
            UserObject->RelativeId, Status);
        return Status;
    }

    if (Data == NULL)
        return STATUS_SUCCESS;

    if (Length == sizeof(ENCRYPTED_NT_OWF_PASSWORD))
    {
        RtlCopyMemory(NtPassword, Data, sizeof(ENCRYPTED_NT_OWF_PASSWORD));
        *NtPasswordPresent = !RtlEqualMemory(NtPassword,
                                             &EmptyNtHash,
                                             sizeof(ENCRYPTED_NT_OWF_PASSWORD));
    }
    else
    {
        Status = STATUS_INTERNAL_DB_CORRUPTION;
    }

    RtlSecureZeroMemory(Data, Length);
    midl_user_free(Data);

    return Status;
}


NTSTATUS
SampSetUserPassword(IN PSAM_DB_OBJECT UserObject,
                    IN PENCRYPTED_NT_OWF_PASSWORD NtPassword,
                    IN BOOLEAN NtPasswordPresent)
{
    SAM_DOMAIN_FIXED_DATA DomainFixedData;
    ENCRYPTED_NT_OWF_PASSWORD NewPassword;
    PENCRYPTED_NT_OWF_PASSWORD OldHistory = NULL;
    PENCRYPTED_NT_OWF_PASSWORD NewHistory = NULL;
    ULONG OldHistoryLength = 0;
    ULONG OldCount = 0;
    ULONG MaxCount;
    ULONG Length;
    NTSTATUS Status;

    if (NtPasswordPresent && NtPassword != NULL)
        NewPassword = *NtPassword;
    else
        NewPassword = EmptyNtHash;

    Length = sizeof(SAM_DOMAIN_FIXED_DATA);
    Status = SampGetObjectAttribute(UserObject->ParentObject,
                                    L"F",
                                    NULL,
                                    &DomainFixedData,
                                    &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SampSetPasswordAttribute(UserObject,
                                      L"NTPwd",
                                      &NewPassword,
                                      sizeof(ENCRYPTED_NT_OWF_PASSWORD));
    if (!NT_SUCCESS(Status))
        goto done;

    MaxCount = DomainFixedData.PasswordHistoryLength;
    if (MaxCount == 0)
    {
        SampRegDeleteValue(UserObject->KeyHandle, L"NTPwdHistory");
    }
    else if (!RtlEqualMemory(&NewPassword, &EmptyNtHash, sizeof(ENCRYPTED_NT_OWF_PASSWORD)))
    {
        Status = SampGetPasswordAttribute(UserObject,
                                          L"NTPwdHistory",
                                          (PUCHAR *)&OldHistory,
                                          &OldHistoryLength);
        if (!NT_SUCCESS(Status))
        {
            OldHistory = NULL;
            OldHistoryLength = 0;
        }

        OldCount = OldHistoryLength / sizeof(ENCRYPTED_NT_OWF_PASSWORD);
        if (OldCount > MaxCount - 1)
            OldCount = MaxCount - 1;

        NewHistory = midl_user_allocate((OldCount + 1) * sizeof(ENCRYPTED_NT_OWF_PASSWORD));
        if (NewHistory == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto done;
        }

        NewHistory[0] = NewPassword;
        if (OldCount > 0)
        {
            RtlCopyMemory(&NewHistory[1],
                          OldHistory,
                          OldCount * sizeof(ENCRYPTED_NT_OWF_PASSWORD));
        }

        Status = SampSetPasswordAttribute(UserObject,
                                          L"NTPwdHistory",
                                          NewHistory,
                                          (OldCount + 1) * sizeof(ENCRYPTED_NT_OWF_PASSWORD));
        if (!NT_SUCCESS(Status))
            goto done;
    }

    SampRegDeleteValue(UserObject->KeyHandle, L"LMPwd");
    SampRegDeleteValue(UserObject->KeyHandle, L"LMPwdHistory");
    Status = STATUS_SUCCESS;

done:
    RtlSecureZeroMemory(&NewPassword, sizeof(NewPassword));

    if (OldHistory != NULL)
    {
        RtlSecureZeroMemory(OldHistory, OldHistoryLength);
        midl_user_free(OldHistory);
    }

    if (NewHistory != NULL)
    {
        RtlSecureZeroMemory(NewHistory, (OldCount + 1) * sizeof(ENCRYPTED_NT_OWF_PASSWORD));
        midl_user_free(NewHistory);
    }

    return Status;
}


static
NTSTATUS
SampMigrateUserPassword(IN PSAM_DB_OBJECT UserObject)
{
    PUCHAR Data;
    ULONG Length = 0;
    ULONG Index;
    LPWSTR Names[] = {L"NTPwd", L"NTPwdHistory"};
    NTSTATUS Status = STATUS_SUCCESS;

    for (Index = 0; Index < ARRAYSIZE(Names); Index++)
    {
        Length = 0;
        Status = SampGetObjectAttribute(UserObject,
                                        Names[Index],
                                        NULL,
                                        NULL,
                                        &Length);
        if (!NT_SUCCESS(Status) || Length == 0 || (Length % SAM_PASSWORD_IV_SIZE) != 0)
            continue;

        Status = SampGetPasswordAttribute(UserObject,
                                          Names[Index],
                                          &Data,
                                          &Length);
        if (!NT_SUCCESS(Status) || Data == NULL)
            continue;

        Status = SampSetPasswordAttribute(UserObject,
                                          Names[Index],
                                          Data,
                                          Length);

        RtlSecureZeroMemory(Data, Length);
        midl_user_free(Data);

        if (!NT_SUCCESS(Status))
            return Status;
    }

    SampRegDeleteValue(UserObject->KeyHandle, L"LMPwd");
    SampRegDeleteValue(UserObject->KeyHandle, L"LMPwdHistory");

    return STATUS_SUCCESS;
}


NTSTATUS
SampMigrateUserPasswords(VOID)
{
    PSAM_DB_OBJECT ServerObject = NULL;
    PSAM_DB_OBJECT DomainObject = NULL;
    PSAM_DB_OBJECT UserObject;
    HANDLE UsersKeyHandle = NULL;
    WCHAR KeyName[64];
    ULONG Index;
    ULONG Rid;
    NTSTATUS Status;

    Status = SampOpenDbObject(NULL,
                              NULL,
                              L"SAM",
                              0,
                              SamDbServerObject,
                              SAM_SERVER_LOOKUP_DOMAIN,
                              &ServerObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SampOpenDbObject(ServerObject,
                              L"Domains",
                              L"Account",
                              0,
                              SamDbDomainObject,
                              DOMAIN_LOOKUP,
                              &DomainObject);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = SampRegOpenKey(DomainObject->KeyHandle,
                            L"Users",
                            KEY_READ,
                            &UsersKeyHandle);
    if (!NT_SUCCESS(Status))
        goto done;

    for (Index = 0; ; Index++)
    {
        Status = SampRegEnumerateSubKey(UsersKeyHandle,
                                        Index,
                                        ARRAYSIZE(KeyName),
                                        KeyName);
        if (!NT_SUCCESS(Status))
            break;

        if (_wcsicmp(KeyName, L"Names") == 0)
            continue;

        Rid = wcstoul(KeyName, NULL, 16);
        if (Rid == 0)
            continue;

        Status = SampOpenUserObject(DomainObject,
                                    Rid,
                                    0,
                                    &UserObject);
        if (!NT_SUCCESS(Status))
            continue;

        Status = SampMigrateUserPassword(UserObject);
        if (!NT_SUCCESS(Status))
            ERR("Failed to migrate the password of user %lu (Status 0x%08lx)\n", Rid, Status);

        SampCloseDbObject(UserObject);
    }

    Status = STATUS_SUCCESS;

done:
    SampRegCloseKey(&UsersKeyHandle);

    if (DomainObject != NULL)
        SampCloseDbObject(DomainObject);

    if (ServerObject != NULL)
        SampCloseDbObject(ServerObject);

    return Status;
}


static
BOOLEAN
SampIsLockoutForever(IN PSAM_DOMAIN_FIXED_DATA DomainFixedData)
{
    return (DomainFixedData->LockoutDuration.QuadPart == 0) ||
           (DomainFixedData->LockoutDuration.QuadPart == MINLONGLONG);
}


BOOLEAN
SampIsUserLockedOut(IN PSAM_DOMAIN_FIXED_DATA DomainFixedData,
                    IN PSAM_USER_FIXED_DATA UserFixedData,
                    IN PLARGE_INTEGER CurrentTime)
{
    if (!(UserFixedData->UserAccountControl & USER_ACCOUNT_AUTO_LOCKED))
        return FALSE;

    if (SampIsLockoutForever(DomainFixedData))
        return TRUE;

    return CurrentTime->QuadPart <
           (UserFixedData->LastBadPasswordTime.QuadPart - DomainFixedData->LockoutDuration.QuadPart);
}


ULONG
SampGetUserAccountControl(IN PSAM_DB_OBJECT UserObject,
                          IN PSAM_USER_FIXED_DATA UserFixedData)
{
    SAM_DOMAIN_FIXED_DATA DomainFixedData;
    LARGE_INTEGER CurrentTime;
    ULONG Length;

    if (!(UserFixedData->UserAccountControl & USER_ACCOUNT_AUTO_LOCKED))
        return UserFixedData->UserAccountControl;

    Length = sizeof(SAM_DOMAIN_FIXED_DATA);
    if (!NT_SUCCESS(SampGetObjectAttribute(UserObject->ParentObject,
                                           L"F",
                                           NULL,
                                           &DomainFixedData,
                                           &Length)) ||
        !NT_SUCCESS(NtQuerySystemTime(&CurrentTime)))
        return UserFixedData->UserAccountControl;

    if (SampIsUserLockedOut(&DomainFixedData, UserFixedData, &CurrentTime))
        return UserFixedData->UserAccountControl;

    return UserFixedData->UserAccountControl & ~USER_ACCOUNT_AUTO_LOCKED;
}


NTSTATUS
SampRecordBadPassword(IN PSAM_DB_OBJECT UserObject,
                      IN OUT PSAM_USER_FIXED_DATA UserFixedData)
{
    SAM_DOMAIN_FIXED_DATA DomainFixedData;
    LARGE_INTEGER CurrentTime;
    ULONG Length;
    NTSTATUS Status;

    Length = sizeof(SAM_DOMAIN_FIXED_DATA);
    Status = SampGetObjectAttribute(UserObject->ParentObject,
                                    L"F",
                                    NULL,
                                    &DomainFixedData,
                                    &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtQuerySystemTime(&CurrentTime);
    if (!NT_SUCCESS(Status))
        return Status;

    if (SampIsUserLockedOut(&DomainFixedData, UserFixedData, &CurrentTime))
        return STATUS_SUCCESS;

    if (UserFixedData->UserAccountControl & USER_ACCOUNT_AUTO_LOCKED)
    {
        UserFixedData->UserAccountControl &= ~USER_ACCOUNT_AUTO_LOCKED;
        UserFixedData->BadPasswordCount = 0;
    }

    if ((DomainFixedData.LockoutObservationWindow.QuadPart < 0) &&
        (CurrentTime.QuadPart >=
         UserFixedData->LastBadPasswordTime.QuadPart - DomainFixedData.LockoutObservationWindow.QuadPart))
        UserFixedData->BadPasswordCount = 0;

    if (UserFixedData->BadPasswordCount < MAXUSHORT)
        UserFixedData->BadPasswordCount++;

    UserFixedData->LastBadPasswordTime = CurrentTime;

    if ((DomainFixedData.LockoutThreshold != 0) &&
        (UserFixedData->BadPasswordCount >= DomainFixedData.LockoutThreshold) &&
        ((UserObject->RelativeId != DOMAIN_USER_RID_ADMIN) ||
         (DomainFixedData.PasswordProperties & DOMAIN_LOCKOUT_ADMINS)))
    {
        UserFixedData->UserAccountControl |= USER_ACCOUNT_AUTO_LOCKED;
    }

    return SampSetObjectAttribute(UserObject,
                                  L"F",
                                  REG_BINARY,
                                  UserFixedData,
                                  sizeof(SAM_USER_FIXED_DATA));
}


VOID
SampMergeUserAccountControl(IN PSAM_DB_OBJECT UserObject,
                            IN OUT PSAM_USER_FIXED_DATA UserFixedData,
                            IN ULONG UserAccountControl)
{
    ULONG CurrentControl;

    CurrentControl = SampGetUserAccountControl(UserObject, UserFixedData);

    if ((CurrentControl & USER_ACCOUNT_AUTO_LOCKED) &&
        (UserAccountControl & USER_ACCOUNT_AUTO_LOCKED))
    {
        UserFixedData->UserAccountControl = UserAccountControl;
        return;
    }

    if (UserFixedData->UserAccountControl & USER_ACCOUNT_AUTO_LOCKED)
        UserFixedData->BadPasswordCount = 0;

    UserFixedData->UserAccountControl = UserAccountControl & ~USER_ACCOUNT_AUTO_LOCKED;
}


static
BOOLEAN
SampPasswordContainsName(IN PCUNICODE_STRING Password,
                         IN PCUNICODE_STRING Name)
{
    USHORT NameLength;
    USHORT Start;
    USHORT End;
    USHORT Index;

    NameLength = Name->Length / sizeof(WCHAR);
    Start = 0;

    while (Start < NameLength)
    {
        End = Start;
        while (End < NameLength && wcschr(L",.-_# \t", Name->Buffer[End]) == NULL)
            End++;

        if (End - Start >= 3)
        {
            for (Index = 0; Index + (End - Start) <= Password->Length / sizeof(WCHAR); Index++)
            {
                if (_wcsnicmp(&Password->Buffer[Index], &Name->Buffer[Start], End - Start) == 0)
                    return TRUE;
            }
        }

        Start = End + 1;
    }

    return FALSE;
}


NTSTATUS
SampCheckPasswordPolicy(IN PSAM_DB_OBJECT UserObject,
                        IN PCUNICODE_STRING Password,
                        IN PENCRYPTED_NT_OWF_PASSWORD NtPassword,
                        IN BOOLEAN Change)
{
    SAM_DOMAIN_FIXED_DATA DomainFixedData;
    SAM_USER_FIXED_DATA UserFixedData;
    ENCRYPTED_NT_OWF_PASSWORD StoredPassword;
    BOOLEAN StoredPasswordPresent = FALSE;
    RPC_UNICODE_STRING AccountName = {0, 0, NULL};
    RPC_UNICODE_STRING FullName = {0, 0, NULL};
    PENCRYPTED_NT_OWF_PASSWORD History = NULL;
    ULONG HistoryLength = 0;
    LARGE_INTEGER CurrentTime;
    LPWORD CharTypes = NULL;
    ULONG PasswordLength;
    ULONG Categories;
    ULONG Upper = 0, Lower = 0, Digit = 0, Punct = 0, Alpha = 0;
    ULONG Length;
    ULONG Index;
    NTSTATUS Status;

    Length = sizeof(SAM_DOMAIN_FIXED_DATA);
    Status = SampGetObjectAttribute(UserObject->ParentObject,
                                    L"F",
                                    NULL,
                                    &DomainFixedData,
                                    &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    Length = sizeof(SAM_USER_FIXED_DATA);
    Status = SampGetObjectAttribute(UserObject,
                                    L"F",
                                    NULL,
                                    &UserFixedData,
                                    &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    PasswordLength = (Password != NULL) ? Password->Length / sizeof(WCHAR) : 0;
    if (PasswordLength > 256)
        return STATUS_PASSWORD_RESTRICTION;

    if (Change)
    {
        Status = SampGetUserNtPassword(UserObject,
                                       &StoredPassword,
                                       &StoredPasswordPresent);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (Change && StoredPasswordPresent && DomainFixedData.MinPasswordAge.QuadPart < 0)
    {
        Status = NtQuerySystemTime(&CurrentTime);
        if (!NT_SUCCESS(Status))
            return Status;

        if (CurrentTime.QuadPart <
            UserFixedData.PasswordLastSet.QuadPart - DomainFixedData.MinPasswordAge.QuadPart)
            return STATUS_PASSWORD_RESTRICTION;
    }

    if (Change && DomainFixedData.PasswordHistoryLength > 0)
    {
        if (StoredPasswordPresent &&
            RtlEqualMemory(&StoredPassword, NtPassword, sizeof(ENCRYPTED_NT_OWF_PASSWORD)))
            return STATUS_PASSWORD_RESTRICTION;

        Status = SampGetPasswordAttribute(UserObject,
                                          L"NTPwdHistory",
                                          (PUCHAR *)&History,
                                          &HistoryLength);
        if (!NT_SUCCESS(Status))
            return Status;

        for (Index = 0;
             Index < HistoryLength / sizeof(ENCRYPTED_NT_OWF_PASSWORD) &&
             Index < DomainFixedData.PasswordHistoryLength;
             Index++)
        {
            if (RtlEqualMemory(&History[Index], NtPassword, sizeof(ENCRYPTED_NT_OWF_PASSWORD)))
            {
                Status = STATUS_PASSWORD_RESTRICTION;
                break;
            }
        }

        if (History != NULL)
        {
            RtlSecureZeroMemory(History, HistoryLength);
            midl_user_free(History);
        }

        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (Password == NULL)
        return STATUS_SUCCESS;

    if ((PasswordLength == 0) &&
        (UserFixedData.UserAccountControl & USER_PASSWORD_NOT_REQUIRED))
        return STATUS_SUCCESS;

    if (PasswordLength < DomainFixedData.MinPasswordLength)
        return STATUS_PASSWORD_RESTRICTION;

    if (!(DomainFixedData.PasswordProperties & DOMAIN_PASSWORD_COMPLEX))
        return STATUS_SUCCESS;

    if (PasswordLength == 0)
        return STATUS_PASSWORD_RESTRICTION;

    Status = SampGetObjectAttributeString(UserObject, L"Name", &AccountName);
    if (!NT_SUCCESS(Status))
        goto done;

    Status = SampGetObjectAttributeString(UserObject, L"FullName", &FullName);
    if (!NT_SUCCESS(Status))
        goto done;

    if (AccountName.Length >= 3 * sizeof(WCHAR))
    {
        for (Index = 0; Index + AccountName.Length / sizeof(WCHAR) <= PasswordLength; Index++)
        {
            if (_wcsnicmp(&Password->Buffer[Index],
                          AccountName.Buffer,
                          AccountName.Length / sizeof(WCHAR)) == 0)
            {
                Status = STATUS_PASSWORD_RESTRICTION;
                goto done;
            }
        }
    }

    if (SampPasswordContainsName(Password, (PCUNICODE_STRING)&FullName))
    {
        Status = STATUS_PASSWORD_RESTRICTION;
        goto done;
    }

    CharTypes = midl_user_allocate(PasswordLength * sizeof(WORD));
    if (CharTypes == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    if (!GetStringTypeW(CT_CTYPE1, Password->Buffer, PasswordLength, CharTypes))
    {
        Status = STATUS_PASSWORD_RESTRICTION;
        goto done;
    }

    for (Index = 0; Index < PasswordLength; Index++)
    {
        if (CharTypes[Index] & C1_UPPER)
            Upper = 1;
        else if (CharTypes[Index] & C1_LOWER)
            Lower = 1;
        else if (CharTypes[Index] & C1_DIGIT)
            Digit = 1;
        else if (CharTypes[Index] & C1_ALPHA)
            Alpha = 1;
        else
            Punct = 1;
    }

    Categories = Upper + Lower + Digit + Punct + Alpha;
    if (Categories < 3)
        Status = STATUS_PASSWORD_RESTRICTION;

done:
    if (CharTypes != NULL)
        midl_user_free(CharTypes);

    if (AccountName.Buffer != NULL)
        midl_user_free(AccountName.Buffer);

    if (FullName.Buffer != NULL)
        midl_user_free(FullName.Buffer);

    return Status;
}


NTSTATUS
SampGetLogonHoursAttribute(IN PSAM_DB_OBJECT UserObject,
                          IN OUT PSAMPR_LOGON_HOURS LogonHours)
{
    PUCHAR RawBuffer = NULL;
    ULONG Length = 0;
    ULONG BufferLength = 0;
    NTSTATUS Status;

    Status = SampGetObjectAttribute(UserObject,
                                    L"LogonHours",
                                    NULL,
                                    NULL,
                                    &Length);
    if (Status != STATUS_BUFFER_OVERFLOW)
    {
        TRACE("SampGetObjectAttribute failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    Status = STATUS_SUCCESS;

    if (Length == 0)
    {
        LogonHours->UnitsPerWeek = 0;
        LogonHours->LogonHours = NULL;
    }
    else
    {
        RawBuffer = midl_user_allocate(Length);
        if (RawBuffer == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto done;
        }

        Status = SampGetObjectAttribute(UserObject,
                                        L"LogonHours",
                                        NULL,
                                        (PVOID)RawBuffer,
                                        &Length);
        if (!NT_SUCCESS(Status))
            goto done;

        LogonHours->UnitsPerWeek = *((PUSHORT)RawBuffer);

        BufferLength = (((ULONG)LogonHours->UnitsPerWeek) + 7) / 8;

        LogonHours->LogonHours = midl_user_allocate(BufferLength);
        if (LogonHours->LogonHours == NULL)
        {
            TRACE("Failed to allocate LogonHours buffer!\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto done;
        }

        memcpy(LogonHours->LogonHours,
               &(RawBuffer[2]),
               BufferLength);
    }

done:

    if (RawBuffer != NULL)
        midl_user_free(RawBuffer);

    return Status;
}


NTSTATUS
SampSetLogonHoursAttribute(IN PSAM_DB_OBJECT UserObject,
                          IN PSAMPR_LOGON_HOURS LogonHours)
{
    PUCHAR RawBuffer = NULL;
    ULONG BufferLength;
    ULONG Length = 0;
    NTSTATUS Status;

    if (LogonHours->UnitsPerWeek > 0)
    {
        BufferLength = (((ULONG)LogonHours->UnitsPerWeek) + 7) / 8;

        Length = BufferLength + sizeof(USHORT);

        RawBuffer = midl_user_allocate(Length);
        if (RawBuffer == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto done;
        }

        *((PUSHORT)RawBuffer) = LogonHours->UnitsPerWeek;

        memcpy(&(RawBuffer[2]),
               LogonHours->LogonHours,
               BufferLength);
    }

    Status = SampSetObjectAttribute(UserObject,
                                    L"LogonHours",
                                    REG_BINARY,
                                    RawBuffer,
                                    Length);

done:
    if (RawBuffer != NULL)
        midl_user_free(RawBuffer);

    return Status;
}

/* EOF */
