/*
 * PROJECT:     ReactOS apisets
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Resolving the apiset to a ReactOS system dll
 * COPYRIGHT:   Copyright 2024 Mark Jansen <mark.jansen@reactos.org>
 */

#include <ndk/umtypes.h>
#include <ndk/rtlfuncs.h>
#include "apisetsp.h"


const ULONGLONG API_ = (ULONGLONG)0x2D004900500041; /// L"API-"
const ULONGLONG EXT_ = (ULONGLONG)0x2D005400580045; /// L"EXT-";

WORD PrefixSize = sizeof(L"api-") - sizeof(WCHAR);
WORD ExtensionSize = sizeof(L".dll") - sizeof(WCHAR);


// The official prototype according to the Windows kit 8.1 is:
//NTSTATUS
//ApiSetResolveToHost (
//    _In_ PCAPI_SET_NAMESPACE_ARRAY Schema,
//    _In_ PCUNICODE_STRING FileNameIn,
//    _In_opt_ PCUNICODE_STRING ParentName,
//    _Out_ PBOOLEAN Resolved,
//    _Out_ PUNICODE_STRING HostBinary
//    );


// Strip the minor version: api-ms-win-core-file-l1-2-1 -> api-ms-win-core-file-l1-2
static
BOOLEAN
ApiSetContractName(
    _In_ PCUNICODE_STRING Name,
    _Out_ PUNICODE_STRING Contract)
{
    USHORT Length = Name->Length / sizeof(WCHAR);
    USHORT Index = Length;

    while (Index > 0 && Name->Buffer[Index - 1] >= L'0' && Name->Buffer[Index - 1] <= L'9')
        Index--;
    if (Index == Length || Index < 2 || Name->Buffer[Index - 1] != L'-')
        return FALSE;

    Contract->Buffer = Name->Buffer;
    Contract->Length = (USHORT)((Index - 1) * sizeof(WCHAR));
    Contract->MaximumLength = Contract->Length;
    return TRUE;
}

static
ULONG
ApiSetMinorVersion(
    _In_ PCUNICODE_STRING Name,
    _In_ PCUNICODE_STRING Contract)
{
    USHORT Index = Contract->Length / sizeof(WCHAR) + 1;
    ULONG Minor = 0;

    while (Index < Name->Length / sizeof(WCHAR))
        Minor = Minor * 10 + (Name->Buffer[Index++] - L'0');
    return Minor;
}

static
VOID
ApiSetScanContracts(
    _In_ const ROS_APISET *Table,
    _In_ LONG Count,
    _In_ DWORD ApisetVersion,
    _In_ PCUNICODE_STRING Contract,
    _Inout_ PCUNICODE_STRING *Target,
    _Inout_ PULONG TargetMinor)
{
    UNICODE_STRING Candidate;
    ULONG CandidateMinor;
    LONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (!(Table[Index].dwOsVersions & ApisetVersion) ||
            !ApiSetContractName(&Table[Index].Name, &Candidate) ||
            !RtlEqualUnicodeString(Contract, &Candidate, TRUE))
        {
            continue;
        }
        CandidateMinor = ApiSetMinorVersion(&Table[Index].Name, &Candidate);
        if (*Target == NULL || CandidateMinor > *TargetMinor)
        {
            *Target = &Table[Index].Target;
            *TargetMinor = CandidateMinor;
        }
    }
}

NTSTATUS
ApiSetQuerySchemaPresence(
    _In_ DWORD ApisetVersion,
    _In_ PCUNICODE_STRING ApiToQuery,
    _Out_ PBOOLEAN InSchema,
    _Out_ PBOOLEAN Present)
{
    USHORT Length = ApiToQuery->Length / sizeof(WCHAR);
    PCUNICODE_STRING Target = NULL;
    UNICODE_STRING Contract;
    ULONG Minor, TargetMinor = 0;
    ULONGLONG Prefix;
    USHORT Char;

    for (Char = 0; Char < Length; Char++)
    {
        if (ApiToQuery->Buffer[Char] == L'.')
            return STATUS_INVALID_PARAMETER;
    }
    if (ApiToQuery->Length <= PrefixSize)
        return STATUS_INVALID_PARAMETER;
    Prefix = ((ULONGLONG *)ApiToQuery->Buffer)[0] & 0xFFFFFFDFFFDFFFDF;
    if (Prefix != API_ && Prefix != EXT_)
        return STATUS_INVALID_PARAMETER;

    *InSchema = FALSE;
    *Present = FALSE;
    if (!ApiSetContractName(ApiToQuery, &Contract))
        return STATUS_SUCCESS;
    Minor = ApiSetMinorVersion(ApiToQuery, &Contract);

    ApiSetScanContracts(g_Apisets, g_ApisetsCount, ApisetVersion, &Contract, &Target, &TargetMinor);
    ApiSetScanContracts(g_ApisetsNoHost, g_ApisetsNoHostCount, ApisetVersion, &Contract, &Target, &TargetMinor);

    if (Target != NULL && Minor <= TargetMinor)
    {
        *InSchema = TRUE;
        *Present = (Target->Length != 0);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ApiSetResolveToHost(
    _In_ DWORD ApisetVersion,
    _In_ PCUNICODE_STRING ApiToResolve,
    _Out_ PBOOLEAN Resolved,
    _Out_ PUNICODE_STRING Output
)
{
    if (ApiToResolve->Length < PrefixSize)
    {
        *Resolved = FALSE;
        return STATUS_SUCCESS;
    }

    // Grab the first four chars from the string, converting the first 3 to uppercase
    PWSTR ApiSetNameBuffer = ApiToResolve->Buffer;
    ULONGLONG ApiSetNameBufferPrefix = ((ULONGLONG *)ApiSetNameBuffer)[0] & 0xFFFFFFDFFFDFFFDF;
    // Check if it matches either 'api-' or 'ext-'
    if (!(ApiSetNameBufferPrefix == API_ || ApiSetNameBufferPrefix == EXT_))
    {
        *Resolved = FALSE;
        return STATUS_SUCCESS;
    }

    // If there is an extension, cut it off (we store apisets without extension)
    UNICODE_STRING Tmp = *ApiToResolve;
    const WCHAR *Extension = Tmp.Buffer + (Tmp.Length - ExtensionSize) / sizeof(WCHAR);
    if (!_wcsnicmp(Extension, L".dll", ExtensionSize / sizeof(WCHAR)))
        Tmp.Length -= ExtensionSize;

    // Binary search the apisets
    // Ideally we should use bsearch here, but that drags in another dependency and we do not want that here.
    LONG UBnd = g_ApisetsCount - 1;
    LONG LBnd = 0;
    while (LBnd <= UBnd)
    {
        LONG Index = (UBnd - LBnd) / 2 + LBnd;

        LONG result = RtlCompareUnicodeString(&Tmp, &g_Apisets[Index].Name, TRUE);
        if (result == 0)
        {
            // Check if this version is included
            if (g_Apisets[Index].dwOsVersions & ApisetVersion)
            {
                // Return a static string (does not have to be freed)
                *Resolved = TRUE;
                *Output = g_Apisets[Index].Target;
                return STATUS_SUCCESS;
            }
            break;
        }
        else if (result < 0)
        {
            UBnd = Index - 1;
        }
        else
        {
            LBnd = Index + 1;
        }
    }

    // Windows 10 resolves a contract whatever minor version is requested,
    // and its schema lists each contract only once.
    if (ApisetVersion & APISET_WIN10)
    {
        UNICODE_STRING Contract, Candidate;

        if (ApiSetContractName(&Tmp, &Contract))
        {
            for (LONG Index = 0; Index < g_ApisetsCount; Index++)
            {
                if ((g_Apisets[Index].dwOsVersions & ApisetVersion) &&
                    ApiSetContractName(&g_Apisets[Index].Name, &Candidate) &&
                    RtlEqualUnicodeString(&Contract, &Candidate, TRUE))
                {
                    *Resolved = TRUE;
                    *Output = g_Apisets[Index].Target;
                    return STATUS_SUCCESS;
                }
            }
        }
    }

    *Resolved = FALSE;
    return STATUS_SUCCESS;
}
