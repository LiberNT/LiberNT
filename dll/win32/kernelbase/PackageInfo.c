/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Package family names and package information references
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <appmodel.h>
#include "rsna_sha256.h"

#define PUBLISHER_ID_LENGTH 13

static
VOID
ComputePublisherId(
    _In_ PCWSTR Publisher,
    _Out_writes_(PUBLISHER_ID_LENGTH) PWSTR PublisherId)
{
    static const WCHAR Alphabet[] = L"0123456789abcdefghjkmnpqrstvwxyz";
    rsna_u8 Digest[RSNA_SHA256_DIGEST_LEN];
    ULONGLONG Value = 0;
    ULONG Index;

    RsnaSha256(Publisher, lstrlenW(Publisher) * sizeof(WCHAR), Digest);
    for (Index = 0; Index < 8; Index++)
        Value = (Value << 8) | Digest[Index];

    for (Index = 0; Index < PUBLISHER_ID_LENGTH - 1; Index++)
        PublisherId[Index] = Alphabet[(Value >> (59 - 5 * Index)) & 31];
    PublisherId[PUBLISHER_ID_LENGTH - 1] = Alphabet[(Value << 1) & 31];
}

LONG
WINAPI
PackageFamilyNameFromId(
    _In_ const PACKAGE_ID *PackageId,
    _Inout_ UINT32 *PackageFamilyNameLength,
    _Out_writes_opt_(*PackageFamilyNameLength) PWSTR PackageFamilyName)
{
    UINT32 NameLength, Required;

    if (!PackageId || !PackageFamilyNameLength || !PackageId->name || !PackageId->publisher)
        return ERROR_INVALID_PARAMETER;

    NameLength = lstrlenW(PackageId->name);
    Required = NameLength + 1 + PUBLISHER_ID_LENGTH + 1;
    if (*PackageFamilyNameLength < Required)
    {
        *PackageFamilyNameLength = Required;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    if (!PackageFamilyName)
        return ERROR_INVALID_PARAMETER;

    CopyMemory(PackageFamilyName, PackageId->name, NameLength * sizeof(WCHAR));
    PackageFamilyName[NameLength] = L'_';
    ComputePublisherId(PackageId->publisher, PackageFamilyName + NameLength + 1);
    PackageFamilyName[Required - 1] = UNICODE_NULL;
    *PackageFamilyNameLength = Required;
    return ERROR_SUCCESS;
}

LONG
WINAPI
OpenPackageInfoByFullName(
    _In_ PCWSTR PackageFullName,
    _Reserved_ const UINT32 Reserved,
    _Out_ PACKAGE_INFO_REFERENCE *PackageInfoReference)
{
    UINT32 Length = 0;

    UNREFERENCED_PARAMETER(Reserved);

    if (!PackageFullName || !PackageInfoReference)
        return ERROR_INVALID_PARAMETER;
    if (PackageIdFromFullName(PackageFullName, 0, &Length, NULL) != ERROR_INSUFFICIENT_BUFFER)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_FOUND;
}

LONG
WINAPI
GetPackageInfo(
    _In_ PACKAGE_INFO_REFERENCE PackageInfoReference,
    _In_ const UINT32 Flags,
    _Inout_ UINT32 *BufferLength,
    _Out_writes_bytes_opt_(*BufferLength) BYTE *Buffer,
    _Out_opt_ UINT32 *Count)
{
    UNREFERENCED_PARAMETER(PackageInfoReference);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Count);

    return ERROR_INVALID_PARAMETER;
}

LONG
WINAPI
ClosePackageInfo(
    _In_ PACKAGE_INFO_REFERENCE PackageInfoReference)
{
    UNREFERENCED_PARAMETER(PackageInfoReference);

    return ERROR_INVALID_PARAMETER;
}
