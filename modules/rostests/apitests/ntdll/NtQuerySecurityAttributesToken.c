/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for NtQuerySecurityAttributesToken and the TokenSecurityAttributes layout
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define UNTOUCHED 0xdeadbeef

typedef struct _TEST_TOKEN_ATTRIBUTE
{
    UNICODE_STRING Name;
    USHORT ValueType;
    USHORT Reserved;
    ULONG Flags;
    ULONG ValueCount;
    PULONG64 Values;
} TEST_TOKEN_ATTRIBUTE, *PTEST_TOKEN_ATTRIBUTE;

typedef struct _TEST_TOKEN_ATTRIBUTES
{
    USHORT Version;
    USHORT Reserved;
    ULONG AttributeCount;
    PTEST_TOKEN_ATTRIBUTE Attributes;
} TEST_TOKEN_ATTRIBUTES, *PTEST_TOKEN_ATTRIBUTES;

static
VOID
CheckProcUnique(
    _In_ const char *Tag,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    PTEST_TOKEN_ATTRIBUTES Info = (PTEST_TOKEN_ATTRIBUTES)Buffer;
    PTEST_TOKEN_ATTRIBUTE Attribute;

    ok(Info->Version == 1, "%s: version %u\n", Tag, Info->Version);
    ok(Info->AttributeCount == 1, "%s: %lu attributes\n", Tag, Info->AttributeCount);
    if (Info->AttributeCount != 1) return;

    Attribute = Info->Attributes;
    ok((PUCHAR)Attribute >= Buffer && (PUCHAR)(Attribute + 1) <= Buffer + Length, "%s: attribute outside the answer\n", Tag);
    ok(Attribute->Name.Length == 32, "%s: name length %u\n", Tag, Attribute->Name.Length);
    ok(Attribute->Name.MaximumLength == Attribute->Name.Length, "%s: name maximum length %u\n", Tag, Attribute->Name.MaximumLength);
    ok(Attribute->Name.Length == 32 && !_wcsnicmp(Attribute->Name.Buffer, L"TSA://ProcUnique", 16), "%s: name %wZ\n", Tag, &Attribute->Name);
    ok(Attribute->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_UINT64, "%s: value type %u\n", Tag, Attribute->ValueType);
    ok(Attribute->Flags == 0x41, "%s: flags 0x%lx\n", Tag, Attribute->Flags);
    ok((PUCHAR)Attribute->Values >= Buffer && (PUCHAR)(Attribute->Values + 2) <= Buffer + Length, "%s: values outside the answer\n", Tag);
    ok(Attribute->ValueCount == 2, "%s: %lu values\n", Tag, Attribute->ValueCount);
    if (sizeof(PVOID) == 8)
        ok(Length == 104, "%s: %lu bytes\n", Tag, Length);
}

START_TEST(NtQuerySecurityAttributesToken)
{
    UNICODE_STRING Names[2];
    HANDLE Token = NULL, Limited = NULL;
    UCHAR Buffer[512];
    ULONG Length;
    NTSTATUS Status;

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &Token);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Length = UNTOUCHED;
    ok_ntstatus(NtQuerySecurityAttributesToken(Token, NULL, 0, NULL, 0, &Length), STATUS_BUFFER_TOO_SMALL);
    if (sizeof(PVOID) == 8) ok_long(Length, 104);
    Length = UNTOUCHED;
    ok_ntstatus(NtQuerySecurityAttributesToken(Token, NULL, 0, Buffer, 8, &Length), STATUS_BUFFER_TOO_SMALL);
    if (sizeof(PVOID) == 8) ok_long(Length, 104);
    Length = UNTOUCHED;
    Status = NtQuerySecurityAttributesToken(Token, NULL, 0, Buffer, sizeof(Buffer), &Length);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) CheckProcUnique("all", Buffer, Length);

    RtlInitUnicodeString(&Names[0], L"TSA://ProcUnique");
    RtlInitUnicodeString(&Names[1], L"NoSuchAttribute");
    Length = UNTOUCHED;
    Status = NtQuerySecurityAttributesToken(Token, Names, 1, Buffer, sizeof(Buffer), &Length);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) CheckProcUnique("named", Buffer, Length);
    Length = UNTOUCHED;
    ok_ntstatus(NtQuerySecurityAttributesToken(Token, Names, 1, NULL, 0, &Length), STATUS_BUFFER_TOO_SMALL);
    if (sizeof(PVOID) == 8) ok_long(Length, 104);
    Length = UNTOUCHED;
    ok_ntstatus(NtQuerySecurityAttributesToken(Token, Names + 1, 1, Buffer, sizeof(Buffer), &Length), STATUS_NOT_FOUND);
    ok_long(Length, 0);
    Length = UNTOUCHED;
    ok_ntstatus(NtQuerySecurityAttributesToken(Token, Names, 2, Buffer, sizeof(Buffer), &Length), STATUS_NOT_FOUND);
    ok_long(Length, 0);
    RtlInitUnicodeString(&Names[1], L"tsa://procunique");
    Length = UNTOUCHED;
    Status = NtQuerySecurityAttributesToken(Token, Names + 1, 1, Buffer, sizeof(Buffer), &Length);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) CheckProcUnique("lowercase", Buffer, Length);

    Length = UNTOUCHED;
    ok_ntstatus(NtQuerySecurityAttributesToken(Token, NULL, 1, Buffer, sizeof(Buffer), &Length), STATUS_INVALID_PARAMETER);
    ok_long(Length, UNTOUCHED);
    ok_ntstatus(NtQuerySecurityAttributesToken(Token, Names, 1, Buffer, sizeof(Buffer), NULL), STATUS_ACCESS_VIOLATION);
    Length = UNTOUCHED;
    ok_ntstatus(NtQuerySecurityAttributesToken(NtCurrentProcess(), NULL, 0, Buffer, sizeof(Buffer), &Length), STATUS_OBJECT_TYPE_MISMATCH);
    Length = UNTOUCHED;
    Status = NtQuerySecurityAttributesToken((HANDLE)(LONG_PTR)-4, NULL, 0, Buffer, sizeof(Buffer), &Length);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) CheckProcUnique("pseudo", Buffer, Length);

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY_SOURCE, &Limited);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Length = UNTOUCHED;
        ok_ntstatus(NtQuerySecurityAttributesToken(Limited, NULL, 0, Buffer, sizeof(Buffer), &Length), STATUS_ACCESS_DENIED);
        ok_long(Length, UNTOUCHED);
        NtClose(Limited);
    }

    Length = UNTOUCHED;
    Status = NtQueryInformationToken(Token, TokenSecurityAttributes, Buffer, sizeof(Buffer), &Length);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) CheckProcUnique("information", Buffer, Length);
    Length = UNTOUCHED;
    ok_ntstatus(NtQueryInformationToken(Token, TokenSecurityAttributes, Buffer, 50, &Length), STATUS_BUFFER_TOO_SMALL);
    if (sizeof(PVOID) == 8) ok_long(Length, 104);

    NtClose(Token);
}
