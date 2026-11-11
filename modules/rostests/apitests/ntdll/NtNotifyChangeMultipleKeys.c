/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Test for NtNotifyChangeMultipleKeys
 * COPYRIGHT:   Copyright 2026 Mohammad Amin Mollazadeh <madamin@pm.me>
 */

#include "precomp.h"
#include <winreg.h>

/* Registry watcher thread for testing synchronous mode */
typedef struct _WATCH_THREAD_CONTEXT
{
    NTSTATUS Status;
    HANDLE KeyHandle;
    PIO_STATUS_BLOCK IoStatusBlock;
} WATCH_THREAD_CONTEXT, *PWATCH_THREAD_CONTEXT;

DWORD WINAPI
NtNotifyChangeMultipleKeys_WatchThread(LPVOID lpParameter)
{
    PWATCH_THREAD_CONTEXT State = (PWATCH_THREAD_CONTEXT)lpParameter;

    State->Status = NtNotifyChangeMultipleKeys(State->KeyHandle, 0,
                                               NULL, NULL, NULL, NULL,
                                               State->IoStatusBlock,
                                               REG_NOTIFY_CHANGE_LAST_SET,
                                               FALSE, NULL, 0, FALSE);

    return 0;
}

static
BOOLEAN
IsThreadWaiting(
    _In_ DWORD ThreadId)
{
    PSYSTEM_PROCESS_INFORMATION Process;
    PSYSTEM_THREAD_INFORMATION Thread;
    BOOLEAN Blocked = FALSE;
    ULONG Size = 0x40000;
    PVOID Buffer = NULL;
    NTSTATUS Status = STATUS_INFO_LENGTH_MISMATCH;
    ULONG Index;

    while (Status == STATUS_INFO_LENGTH_MISMATCH)
    {
        if (Buffer) HeapFree(GetProcessHeap(), 0, Buffer);
        Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
        if (Buffer == NULL) return FALSE;
        Status = NtQuerySystemInformation(SystemProcessInformation, Buffer, Size, NULL);
        Size *= 2;
    }

    Process = Buffer;
    while (NT_SUCCESS(Status) &&
           Process->UniqueProcessId != UlongToHandle(GetCurrentProcessId()) &&
           Process->NextEntryOffset != 0)
    {
        Process = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)Process + Process->NextEntryOffset);
    }

    if (NT_SUCCESS(Status) && Process->UniqueProcessId == UlongToHandle(GetCurrentProcessId()))
    {
        Thread = (PSYSTEM_THREAD_INFORMATION)(Process + 1);
        for (Index = 0; Index < Process->NumberOfThreads; Index++)
        {
            if (Thread[Index].ClientId.UniqueThread == UlongToHandle(ThreadId))
                Blocked = (Thread[Index].ThreadState == Waiting);
        }
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    return Blocked;
}

/* The watcher has registered once it waits; polling for that replaces a fixed sleep */
static
BOOLEAN
WaitUntilWatcherBlocks(
    _In_ HANDLE ThreadHandle,
    _In_ DWORD ThreadId)
{
    ULONGLONG Deadline = GetTickCount64() + 5000;

    while (WaitForSingleObject(ThreadHandle, 0) == WAIT_TIMEOUT && GetTickCount64() < Deadline)
    {
        if (IsThreadWaiting(ThreadId)) return TRUE;
        SwitchToThread();
    }

    return FALSE;
}

typedef struct _APC_CONTEXT
{
    BOOLEAN ApcRan;
} APC_CONTEXT, *PAPC_CONTEXT;

/* APC Routine for testing asynchronous mode */
VOID WINAPI
NtNotifyChangeMultipleKeys_ApcRoutine(PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG Reserved)
{
    UNREFERENCED_PARAMETER(IoStatusBlock);
    UNREFERENCED_PARAMETER(Reserved);

    PAPC_CONTEXT ApcState = ApcContext;
    ApcState->ApcRan = TRUE;
}

START_TEST(NtNotifyChangeMultipleKeys)
{
    NTSTATUS Status;
    DWORD WaitStatus;
    DWORD WatchThreadId = 0;
    IO_STATUS_BLOCK IoStatusBlock = { 0 };
    OBJECT_ATTRIBUTES SubordinateObjects[1];

    PWATCH_THREAD_CONTEXT WatchThread1State = NULL, WatchThread2State = NULL;
    PAPC_CONTEXT ApcContext = NULL;

    /* Registry key object attributes */
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\TestKey");
    UNICODE_STRING SubKeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\TestKey\\TestSubKey");
    UNICODE_STRING SecondaryKeyName = RTL_CONSTANT_STRING(L"\\Registry\\User\\.DEFAULT\\SOFTWARE\\TestKey");
    UNICODE_STRING ThirdKeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software\\Microsoft\\Windows");
    OBJECT_ATTRIBUTES ObjectAttributes = RTL_CONSTANT_OBJECT_ATTRIBUTES(&KeyName, OBJ_CASE_INSENSITIVE);
    OBJECT_ATTRIBUTES SubKeyObjectAttributes = RTL_CONSTANT_OBJECT_ATTRIBUTES(&SubKeyName, OBJ_CASE_INSENSITIVE);
    OBJECT_ATTRIBUTES SecondaryObjectAttributes = RTL_CONSTANT_OBJECT_ATTRIBUTES(&SecondaryKeyName, OBJ_CASE_INSENSITIVE);
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"TestValue");
    DWORD Value1 = 0x12345678, Value2 = 0x87654321;

    /* Handles */
    HANDLE KeyHandle = NULL, SubKeyHandle = NULL, SecondaryKeyHandle = NULL;
    HANDLE WatchThread1Handle = NULL, WatchThread2Handle = NULL;
    HANDLE EventHandle = NULL;

    /* Create event */
    EventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (EventHandle == NULL)
    {
        skip("Failed to create event");
        return;
    }
    /* Create registry keys */
    Status = NtCreateKey(&KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create registry key");
        goto Cleanup;
    }
    Status = NtSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD, &Value1, sizeof(Value1));
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to set initial test value");
        goto Cleanup;
    }

    /* Synchronous mode */

    /* Create a thread */
    WatchThread1State = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*WatchThread1State));
    if (WatchThread1State == NULL)
    {
        skip("Failed to allocate memory for watch thread state");
        goto Cleanup;
    }
    WatchThread1State->KeyHandle = KeyHandle;
    WatchThread1State->IoStatusBlock = &IoStatusBlock;
    WatchThread1Handle = CreateThread(NULL, 0, NtNotifyChangeMultipleKeys_WatchThread, WatchThread1State, 0, &WatchThreadId);
    if (WatchThread1Handle)
    {
        /* Verify the thread is still running */
        ok(WaitUntilWatcherBlocks(WatchThread1Handle, WatchThreadId), "Watcher did not block\n");
        WaitStatus = WaitForSingleObject(WatchThread1Handle, 0);
        ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
        /* Make change to the registry key */
        Status = NtSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD, &Value2, sizeof(Value2));
        ok_ntstatus(Status, STATUS_SUCCESS);
        /* Verify that the thread is notified */
        WaitStatus = WaitForSingleObject(WatchThread1Handle, 100);
        ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
        /* Verify the status code */
        ok_ntstatus(WatchThread1State->Status, STATUS_NOTIFY_ENUM_DIR);
        ok_ntstatus(WatchThread1State->IoStatusBlock->Status, STATUS_NOTIFY_ENUM_DIR);
        /* cleanup */
        CloseHandle(WatchThread1Handle);
        WatchThread1Handle = NULL;
    }
    else
    {
        skip("Failed to create watch thread");
    }
    HeapFree(GetProcessHeap(), 0, WatchThread1State);
    WatchThread1State = NULL;
    /* Watch again, but this time close the handle without making any change */
    WatchThread2State = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*WatchThread2State));
    if (WatchThread2State == NULL)
    {
        skip("Failed to allocate memory for watch thread state");
        goto Cleanup;
    }
    WatchThread2State->KeyHandle = KeyHandle;
    WatchThread2State->IoStatusBlock = &IoStatusBlock;
    WatchThread2Handle = CreateThread(NULL, 0, NtNotifyChangeMultipleKeys_WatchThread, WatchThread2State, 0, &WatchThreadId);
    if (WatchThread2Handle)
    {
        ok(WaitUntilWatcherBlocks(WatchThread2Handle, WatchThreadId), "Watcher did not block\n");
        WaitStatus = WaitForSingleObject(WatchThread2Handle, 0);
        ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
        NtClose(KeyHandle);
        KeyHandle = NULL;
        WaitStatus = WaitForSingleObject(WatchThread2Handle, 100);
        ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
        ok_ntstatus(WatchThread2State->Status, STATUS_NOTIFY_CLEANUP);
        ok_ntstatus(WatchThread2State->IoStatusBlock->Status, STATUS_NOTIFY_CLEANUP);
        /* cleanup */
        CloseHandle(WatchThread2Handle);
        WatchThread2Handle = NULL;
    }
    else
    {
        skip("Failed to create watch thread");
    }
    HeapFree(GetProcessHeap(), 0, WatchThread2State);
    WatchThread2State = NULL;

    /* Event-based asynchronous mode */

    if (!KeyHandle)
    {
        Status = NtOpenKey(&KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            skip("Failed to open registry key");
            goto Cleanup;
        }
    }
    /* Start watching for changes */
    Status = NtNotifyChangeMultipleKeys(KeyHandle, 0, NULL,
                                        EventHandle, NULL, NULL,
                                        &IoStatusBlock,
                                        REG_NOTIFY_CHANGE_LAST_SET,
                                        FALSE, NULL, 0, TRUE);
    ok_ntstatus(Status, STATUS_PENDING);
    /* Check event state */
    WaitStatus = WaitForSingleObject(EventHandle, 0);
    ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
    /* Make change to the registry key */
    Status = NtSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD, &Value1, sizeof(Value1));
    ok_ntstatus(Status, STATUS_SUCCESS);
    /* Verify that the event is signaled */
    NtTestAlert();
    WaitStatus = WaitForSingleObject(EventHandle, 100);
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    /* Verify the status code */
    ok_ntstatus(IoStatusBlock.Status, STATUS_NOTIFY_ENUM_DIR);

    /* Watch again, but this time close the handle without making any change */
    Status = NtNotifyChangeMultipleKeys(KeyHandle, 0, NULL,
                                        EventHandle, NULL, NULL,
                                        &IoStatusBlock,
                                        REG_NOTIFY_CHANGE_LAST_SET,
                                        FALSE, NULL, 0, TRUE);
    ok_ntstatus(Status, STATUS_PENDING);
    WaitStatus = WaitForSingleObject(EventHandle, 0);
    ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
    NtClose(KeyHandle);
    KeyHandle = NULL;
    NtTestAlert();
    WaitStatus = WaitForSingleObject(EventHandle, 100);
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    ok_ntstatus(IoStatusBlock.Status, STATUS_NOTIFY_CLEANUP);

    /* Watching subtree */

    if (!KeyHandle)
    {
        Status = NtOpenKey(&KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            skip("Failed to open registry key");
            goto Cleanup;
        }
    }
    Status = NtCreateKey(&SubKeyHandle, KEY_ALL_ACCESS, &SubKeyObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
    if (NT_SUCCESS(Status))
    {
        /* Start watching for changes */
        Status = NtNotifyChangeMultipleKeys(KeyHandle, 0, NULL,
                                            EventHandle, NULL, NULL,
                                            &IoStatusBlock,
                                            REG_NOTIFY_CHANGE_LAST_SET,
                                            TRUE, NULL, 0, TRUE);
        ok_ntstatus(Status, STATUS_PENDING);
        /* Check event state */
        WaitStatus = WaitForSingleObject(EventHandle, 0);
        ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
        /* Make change to the subkey */
        Status = NtSetValueKey(SubKeyHandle, &ValueName, 0, REG_DWORD, &Value1, sizeof(Value1));
        ok_ntstatus(Status, STATUS_SUCCESS);
        /* Verify that the event is signaled */
        NtTestAlert();
        WaitStatus = WaitForSingleObject(EventHandle, 100);
        ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    }
    else
    {
        skip("Failed to create subkey");
    }

    /* Watching multiple keys */

    Status = NtCreateKey(&SecondaryKeyHandle, KEY_ALL_ACCESS, &SecondaryObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
    if (NT_SUCCESS(Status))
    {
        NtClose(SecondaryKeyHandle);
        InitializeObjectAttributes(&SubordinateObjects[0], &SecondaryKeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        Status = NtNotifyChangeMultipleKeys(KeyHandle,
                                            _countof(SubordinateObjects),
                                            SubordinateObjects,
                                            EventHandle,
                                            NULL,
                                            NULL,
                                            &IoStatusBlock,
                                            REG_NOTIFY_CHANGE_LAST_SET,
                                            TRUE,
                                            NULL,
                                            0,
                                            TRUE);
        ok_ntstatus(Status, STATUS_PENDING);
        /* Check event state */
        WaitStatus = WaitForSingleObject(EventHandle, 0);
        ok_eq_ulong(WaitStatus, WAIT_TIMEOUT);
        /* Make change to the secondary key */
        Status = NtOpenKey(&SecondaryKeyHandle, KEY_SET_VALUE | KEY_NOTIFY | DELETE, &SecondaryObjectAttributes);
        if (NT_SUCCESS(Status))
        {
            Status = NtSetValueKey(SecondaryKeyHandle, &ValueName, 0, REG_DWORD, &Value1, sizeof(Value1));
            ok_ntstatus(Status, STATUS_SUCCESS);
            NtClose(SecondaryKeyHandle);
        }
        /* Verify that the event is signaled */
        NtTestAlert();
        WaitStatus = WaitForSingleObject(EventHandle, 100);
        ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    }
    else
    {
        skip("Failed to create secondary key");
    }
    /* Test if the function fails if the given subordinate key is same with master key */
    InitializeObjectAttributes(&SubordinateObjects[0], &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtNotifyChangeMultipleKeys(KeyHandle,
                                        _countof(SubordinateObjects),
                                        SubordinateObjects,
                                        EventHandle,
                                        NULL,
                                        NULL,
                                        &IoStatusBlock,
                                        REG_NOTIFY_CHANGE_LAST_SET,
                                        TRUE,
                                        NULL,
                                        0,
                                        TRUE);
    /* The status is STATUS_INVALID_PARAMETER on Windows Server 2003 and STATUS_INVALID_OBJECT_NAME on Windows 10 */
    ok(!NT_SUCCESS(Status), "NtNotifyChangeMultipleKeys succeeded unexpectedly.\n");
    /* Test if the function fails if the the given subordinate key is in the same hive as the master key */
    InitializeObjectAttributes(&SubordinateObjects[0], &ThirdKeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtNotifyChangeMultipleKeys(KeyHandle,
                                        _countof(SubordinateObjects),
                                        SubordinateObjects,
                                        EventHandle,
                                        NULL,
                                        NULL,
                                        &IoStatusBlock,
                                        REG_NOTIFY_CHANGE_LAST_SET,
                                        TRUE,
                                        NULL,
                                        0,
                                        TRUE);
    ok(!NT_SUCCESS(Status), "NtNotifyChangeMultipleKeys succeeded unexpectedly.\n");

    /* APC-based asynchronous mode */
    ApcContext = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ApcContext));
    if (ApcContext == NULL)
    {
        skip("Failed to allocate memory for APC context");
        goto Cleanup;
    }
    Status = NtNotifyChangeMultipleKeys(KeyHandle, 0, NULL,
                                        NULL, NtNotifyChangeMultipleKeys_ApcRoutine, ApcContext,
                                        &IoStatusBlock,
                                        REG_NOTIFY_CHANGE_LAST_SET,
                                        FALSE, NULL, 0, TRUE);
    ok_ntstatus(Status, STATUS_PENDING);
    /* Make change to the registry key */
    Status = NtSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD, &Value2, sizeof(Value2));
    ok_ntstatus(Status, STATUS_SUCCESS);
    /* Force system to run queued APC routines */
    NtTestAlert();
    ok(ApcContext->ApcRan == TRUE, "The APC routine did not ran.\n");

Cleanup:
    if (WatchThread1Handle)
    {
        CloseHandle(WatchThread1Handle);
    }
    if (WatchThread2Handle)
    {
        CloseHandle(WatchThread2Handle);
    }
    if (EventHandle)
    {
        CloseHandle(EventHandle);
    }
    if (ApcContext)
    {
        HeapFree(GetProcessHeap(), 0, ApcContext);
    }
    if (SubKeyHandle)
    {
        NtDeleteKey(SubKeyHandle);
        NtClose(SubKeyHandle);
    }
    if (!KeyHandle)
    {
        Status = NtOpenKey(&KeyHandle, DELETE, &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            KeyHandle = NULL;
        }
    }
    if (KeyHandle)
    {
        NtDeleteKey(KeyHandle);
        NtClose(KeyHandle);
    }
    Status = NtOpenKey(&SecondaryKeyHandle, DELETE, &SecondaryObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        NtDeleteKey(SecondaryKeyHandle);
        NtClose(SecondaryKeyHandle);
    }
}
