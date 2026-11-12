/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests RtlCreateServiceSid
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef NTSTATUS (NTAPI *FN_RtlCreateServiceSid)(PUNICODE_STRING, PSID, PULONG);

static FN_RtlCreateServiceSid pRtlCreateServiceSid;

static
VOID
CheckServiceSid(
    _In_ PCWSTR Name,
    _In_ PCWSTR Expected)
{
    UNICODE_STRING ServiceName, String, ExpectedString;
    ULONG Buffer[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    ULONG Length;
    NTSTATUS Status;

    RtlInitUnicodeString(&ServiceName, Name);
    Length = RtlLengthRequiredSid(SECURITY_SERVICE_ID_RID_COUNT);
    RtlFillMemory(Buffer, sizeof(Buffer), 0xcc);
    Status = pRtlCreateServiceSid(&ServiceName, Buffer, &Length);
    ok(Status == STATUS_SUCCESS, "%S: status 0x%lx\n", Name, Status);
    ok(Length == RtlLengthRequiredSid(SECURITY_SERVICE_ID_RID_COUNT), "%S: length %lu\n", Name, Length);
    if (!NT_SUCCESS(Status))
        return;

    ok(RtlValidSid(Buffer), "%S: invalid SID\n", Name);
    Status = RtlConvertSidToUnicodeString(&String, Buffer, TRUE);
    ok(Status == STATUS_SUCCESS, "%S: RtlConvertSidToUnicodeString status 0x%lx\n", Name, Status);
    if (!NT_SUCCESS(Status))
        return;

    RtlInitUnicodeString(&ExpectedString, Expected);
    ok(RtlEqualUnicodeString(&String, &ExpectedString, FALSE), "%S: SID %S, expected %S\n",
       Name, String.Buffer, Expected);
    RtlFreeUnicodeString(&String);
}

START_TEST(RtlCreateServiceSid)
{
    UNICODE_STRING ServiceName;
    ULONG Buffer[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    ULONG Length;
    NTSTATUS Status;

    pRtlCreateServiceSid = (FN_RtlCreateServiceSid)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlCreateServiceSid");
    if (!pRtlCreateServiceSid)
    {
        skip("RtlCreateServiceSid not exported by ntdll\n");
        return;
    }

    RtlInitUnicodeString(&ServiceName, L"TestService");

    Length = 0;
    Status = pRtlCreateServiceSid(NULL, NULL, &Length);
    ok(Status == STATUS_INVALID_PARAMETER, "Status 0x%lx\n", Status);

    Status = pRtlCreateServiceSid(&ServiceName, NULL, NULL);
    ok(Status == STATUS_INVALID_PARAMETER, "Status 0x%lx\n", Status);

    /* A buffer that is too small gets the length it needs */
    Length = 0;
    Status = pRtlCreateServiceSid(&ServiceName, NULL, &Length);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "Status 0x%lx\n", Status);
    ok(Length == RtlLengthRequiredSid(SECURITY_SERVICE_ID_RID_COUNT), "Length %lu\n", Length);

    Length = RtlLengthRequiredSid(SECURITY_SERVICE_ID_RID_COUNT) - 1;
    RtlFillMemory(Buffer, sizeof(Buffer), 0xcc);
    Status = pRtlCreateServiceSid(&ServiceName, Buffer, &Length);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "Status 0x%lx\n", Status);
    ok(Length == RtlLengthRequiredSid(SECURITY_SERVICE_ID_RID_COUNT), "Length %lu\n", Length);
    ok(Buffer[0] == 0xcccccccc, "Buffer was written: 0x%lx\n", Buffer[0]);

    /* A NULL buffer only asks for the length, whatever length is passed */
    Length = sizeof(Buffer);
    Status = pRtlCreateServiceSid(&ServiceName, NULL, &Length);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "Status 0x%lx\n", Status);
    ok(Length == RtlLengthRequiredSid(SECURITY_SERVICE_ID_RID_COUNT), "Length %lu\n", Length);

    /* The SID is S-1-5-80 followed by the SHA-1 of the upper-cased name */
    CheckServiceSid(L"TestService", L"S-1-5-80-3892056402-659729507-4115993473-1921682939-1565901394");
    CheckServiceSid(L"testservice", L"S-1-5-80-3892056402-659729507-4115993473-1921682939-1565901394");
    CheckServiceSid(L"TrustedInstaller", L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464");

    /* A larger buffer is filled the same way and the length is the SID's */
    Length = sizeof(Buffer);
    Status = pRtlCreateServiceSid(&ServiceName, Buffer, &Length);
    ok(Status == STATUS_SUCCESS, "Status 0x%lx\n", Status);
    ok(Length == RtlLengthRequiredSid(SECURITY_SERVICE_ID_RID_COUNT), "Length %lu\n", Length);
}
