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

typedef struct _BASE_WER_REGISTRATION
{
    ULONG Signature;
    LONG State;
    ULONG Flags;
    ULONG RecoveryPingInterval;
    ULONG64 RecoveryCallback;
    ULONG64 RecoveryParameter;
    ULONG RecoveryFlags;
    ULONG Reserved;
    RTL_SRWLOCK Lock;
    HANDLE RecoveryPingEvent;
    LONG RecoveryExitStatus;
    LONG RecoveryCancelled;
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

#endif /* _BASEWER_H_ */
