/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Per-process Windows Error Reporting registration shared by kernel32 and kernelbase
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _BASEWER_H_
#define _BASEWER_H_

#pragma once

#define BASE_WER_REGISTRATION_SIGNATURE 0x52455742

#define BASE_WER_FLAGS_SET              0x00000001
#define BASE_WER_RECOVERY_REGISTERED    0x00000002
#define BASE_WER_RECOVERY_IN_PROGRESS   0x00000004
#define BASE_WER_RESTART_REGISTERED     0x00000008

#define BASE_WER_HANG_EXIT_STATUS       0xCFFFFFFF

typedef struct _BASE_WER_ENTRY
{
    ULONG64 Next;
    ULONG64 Address;
    ULONG Size;
    ULONG Type;
    ULONG Flags;
    ULONG NameLength;
    ULONG64 Name;
} BASE_WER_ENTRY, *PBASE_WER_ENTRY;

typedef struct _BASE_WER_REGISTRATION
{
    ULONG Signature;
    LONG State;
    ULONG Flags;
    ULONG RecoveryPingInterval;
    ULONG64 RecoveryCallback;
    ULONG64 RecoveryParameter;
    ULONG RecoveryFlags;
    ULONG RestartFlags;
    ULONG64 RestartCommandLine;
    ULONG RestartLength;
    ULONG Reserved;
    ULONG64 Files;
    ULONG64 MemoryBlocks;
    ULONG64 RuntimeModules;
    ULONG64 RemoteRecoveryRoutine;
    LONG RecoveryCancelled;
    ULONG Reserved2;
    RTL_SRWLOCK Lock;
    HANDLE RecoveryPingEvent;
    LONG RecoveryExitStatus;
} BASE_WER_REGISTRATION, *PBASE_WER_REGISTRATION;

#define BASE_WER_SHARED_SIZE FIELD_OFFSET(BASE_WER_REGISTRATION, Lock)

PBASE_WER_REGISTRATION
WINAPI
BaseWerGetRegistration(
    _In_ BOOLEAN Create);

LONG
WINAPI
BaseWerGetState(VOID);

HRESULT
WINAPI
BaseWerQueryRegistration(
    _In_ HANDLE Process,
    _Out_ PBASE_WER_REGISTRATION Registration);

HRESULT
WINAPI
BaseWerReadRegistrationMemory(
    _In_ HANDLE Process,
    _In_ ULONG64 Address,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size);

#endif /* _BASEWER_H_ */
