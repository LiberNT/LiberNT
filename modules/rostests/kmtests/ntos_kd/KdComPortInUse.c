/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A debugger on a COM port publishes it in KdComPortInUse and keeps it from the serial driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntstrsafe.h>

__declspec(dllimport) extern PUCHAR KdComPortInUse;

static
BOOLEAN
HasToken(
    _In_reads_(Length) PCWSTR Options,
    _In_ SIZE_T Length,
    _In_ PCWSTR Token,
    _Out_opt_ PWCHAR Following)
{
    SIZE_T TokenLength = wcslen(Token);
    SIZE_T Index = 0;

    while (Index < Length)
    {
        SIZE_T Start;

        while (Index < Length && Options[Index] == L' ')
            Index++;
        Start = Index;
        while (Index < Length && Options[Index] != L' ' && Options[Index] != UNICODE_NULL)
            Index++;
        if (Index - Start >= TokenLength && _wcsnicmp(&Options[Start], Token, TokenLength) == 0 &&
            (Following != NULL || Index - Start == TokenLength))
        {
            if (Following != NULL)
                *Following = (Start + TokenLength < Index) ? Options[Start + TokenLength] : UNICODE_NULL;
            return TRUE;
        }
        if (Index < Length && Options[Index] == UNICODE_NULL)
            break;
    }
    return FALSE;
}

static
ULONG
QueryDebugComPort(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"SystemStartOptions");
    OBJECT_ATTRIBUTES ObjectAttributes;
    PKEY_VALUE_PARTIAL_INFORMATION Info;
    ULONG InfoLength = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 1024 * sizeof(WCHAR);
    ULONG Returned;
    HANDLE Key;
    NTSTATUS Status;
    WCHAR Digit = UNICODE_NULL;
    ULONG Port = 0;

    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return 0;

    Info = ExAllocatePoolWithTag(PagedPool, InfoLength, 'pCdK');
    ok(Info != NULL, "Out of pool\n");
    if (Info != NULL)
    {
        Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Info, InfoLength, &Returned);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status) && Info->Type == REG_SZ)
        {
            PCWSTR Options = (PCWSTR)Info->Data;
            SIZE_T Length = Info->DataLength / sizeof(WCHAR);

            if (HasToken(Options, Length, L"DEBUG", NULL) &&
                HasToken(Options, Length, L"DEBUGPORT=COM", &Digit) &&
                Digit >= L'1' && Digit <= L'9')
            {
                Port = Digit - L'0';
            }
        }
        ExFreePoolWithTag(Info, 'pCdK');
    }
    ZwClose(Key);
    return Port;
}

START_TEST(KdComPortInUse)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Name;
    WCHAR Buffer[16];
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Port;

    Port = QueryDebugComPort();
    if (skip(Port != 0 && KdDebuggerEnabled, "The debugger is not on a COM port\n"))
        return;

    ok(KdComPortInUse != NULL, "KdComPortInUse is NULL with the debugger on COM%lu\n", Port);

    RtlInitEmptyUnicodeString(&Name, Buffer, sizeof(Buffer));
    Status = RtlUnicodeStringPrintf(&Name, L"\\??\\COM%lu", Port);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&Handle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &ObjectAttributes, &IoStatus, 0, FILE_SYNCHRONOUS_IO_NONALERT);
    ok(!NT_SUCCESS(Status), "COM%lu, the debugger's port, was opened through the serial driver\n", Port);
    if (NT_SUCCESS(Status))
        ZwClose(Handle);
}
