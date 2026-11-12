/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Application recovery registration and the recovery run of an unhandled exception
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32_vista.h>
#include <ndk/exfuncs.h>
#include <basewer.h>

#ifndef RECOVERY_DEFAULT_PING_INTERVAL
#define RECOVERY_DEFAULT_PING_INTERVAL 5000
#define RECOVERY_MAX_PING_INTERVAL     (5 * 60 * 1000)
#endif

#define BASE_RECOVERY_UNITS_PER_MS 10000

typedef struct _BASE_RECOVERY_RUN
{
    APPLICATION_RECOVERY_CALLBACK Callback;
    PVOID Parameter;
} BASE_RECOVERY_RUN, *PBASE_RECOVERY_RUN;

static
DWORD
WINAPI
BasepRemoteRecoveryThread(
    _In_ PVOID Parameter)
{
    PBASE_WER_REGISTRATION Registration = BaseWerGetRegistration(FALSE);
    NTSTATUS ExitStatus = (NTSTATUS)BASE_WER_HANG_EXIT_STATUS;
    APPLICATION_RECOVERY_CALLBACK Callback;
    PVOID CallbackParameter;

    if (!Registration)
        NtTerminateProcess(NtCurrentProcess(), ExitStatus);

    RtlAcquireSRWLockExclusive(&Registration->Lock);
    if ((Registration->State & BASE_WER_RECOVERY_IN_PROGRESS) ||
        !(Registration->State & BASE_WER_RECOVERY_REGISTERED))
    {
        RtlReleaseSRWLockExclusive(&Registration->Lock);
        NtTerminateProcess(NtCurrentProcess(), ExitStatus);
        return 0;
    }

    Callback = (APPLICATION_RECOVERY_CALLBACK)(ULONG_PTR)Registration->RecoveryCallback;
    CallbackParameter = (PVOID)(ULONG_PTR)Registration->RecoveryParameter;
    Registration->RecoveryPingEvent = Parameter;
    Registration->RecoveryExitStatus = ExitStatus;
    InterlockedOr(&Registration->State, BASE_WER_RECOVERY_IN_PROGRESS);
    RtlReleaseSRWLockExclusive(&Registration->Lock);

    Callback(CallbackParameter);
    NtTerminateProcess(NtCurrentProcess(), ExitStatus);
    return 0;
}

HRESULT
WINAPI
RegisterApplicationRecoveryCallback(
    _In_ APPLICATION_RECOVERY_CALLBACK pRecoveyCallback,
    _In_opt_ PVOID pvParameter,
    _In_ DWORD dwPingInterval,
    _In_ DWORD dwFlags)
{
    PBASE_WER_REGISTRATION Registration;

    if (!pRecoveyCallback || dwPingInterval > RECOVERY_MAX_PING_INTERVAL)
        return E_INVALIDARG;

    Registration = BaseWerGetRegistration(TRUE);
    if (!Registration)
        return E_FAIL;

    RtlAcquireSRWLockExclusive(&Registration->Lock);
    InterlockedAnd(&Registration->State, ~BASE_WER_RECOVERY_REGISTERED);
    Registration->RecoveryCallback = (ULONG_PTR)pRecoveyCallback;
    Registration->RecoveryParameter = (ULONG_PTR)pvParameter;
    Registration->RecoveryPingInterval = dwPingInterval ? dwPingInterval : RECOVERY_DEFAULT_PING_INTERVAL;
    Registration->RecoveryFlags = dwFlags;
    Registration->RemoteRecoveryRoutine = (ULONG_PTR)BasepRemoteRecoveryThread;
    InterlockedOr(&Registration->State, BASE_WER_RECOVERY_REGISTERED);
    RtlReleaseSRWLockExclusive(&Registration->Lock);

    return S_OK;
}

HRESULT
WINAPI
UnregisterApplicationRecoveryCallback(VOID)
{
    PBASE_WER_REGISTRATION Registration = BaseWerGetRegistration(FALSE);

    if (!Registration)
        return S_OK;

    RtlAcquireSRWLockExclusive(&Registration->Lock);
    InterlockedAnd(&Registration->State, ~BASE_WER_RECOVERY_REGISTERED);
    Registration->RecoveryCallback = 0;
    Registration->RecoveryParameter = 0;
    Registration->RecoveryPingInterval = 0;
    Registration->RecoveryFlags = 0;
    RtlReleaseSRWLockExclusive(&Registration->Lock);

    return S_OK;
}

HRESULT
WINAPI
GetApplicationRecoveryCallback(
    _In_ HANDLE hProcess,
    _Out_ APPLICATION_RECOVERY_CALLBACK *pRecoveryCallback,
    _Outptr_opt_result_maybenull_ PVOID *ppvParameter,
    _Out_opt_ PDWORD pdwPingInterval,
    _Out_opt_ PDWORD pdwFlags)
{
    BASE_WER_REGISTRATION Registration;
    BOOLEAN Registered;
    HRESULT Result;

    if (!hProcess || !pRecoveryCallback)
        return E_INVALIDARG;

    Result = BaseWerQueryRegistration(hProcess, &Registration);
    if (FAILED(Result))
        return Result;

    Registered = (Registration.State & BASE_WER_RECOVERY_REGISTERED) != 0;
    if (!Registered)
        RtlZeroMemory(&Registration, sizeof(Registration));

    *pRecoveryCallback = (APPLICATION_RECOVERY_CALLBACK)(ULONG_PTR)Registration.RecoveryCallback;
    if (ppvParameter)
        *ppvParameter = (PVOID)(ULONG_PTR)Registration.RecoveryParameter;
    if (pdwPingInterval)
        *pdwPingInterval = Registration.RecoveryPingInterval * BASE_RECOVERY_UNITS_PER_MS;
    if (pdwFlags)
        *pdwFlags = Registration.RecoveryFlags;

    return Registered ? S_OK : S_FALSE;
}

HRESULT
WINAPI
ApplicationRecoveryInProgress(
    _Out_ PBOOL pbCancelled)
{
    PBASE_WER_REGISTRATION Registration;

    if (!pbCancelled)
        return E_INVALIDARG;

    Registration = BaseWerGetRegistration(FALSE);
    if (!Registration || !(ReadAcquire(&Registration->State) & BASE_WER_RECOVERY_IN_PROGRESS))
        return E_FAIL;

    if (Registration->RecoveryPingEvent)
        NtSetEvent(Registration->RecoveryPingEvent, NULL);
    *pbCancelled = ReadAcquire(&Registration->RecoveryCancelled) != 0;
    return S_OK;
}

VOID
WINAPI
ApplicationRecoveryFinished(
    _In_ BOOL bSuccess)
{
    PBASE_WER_REGISTRATION Registration = BaseWerGetRegistration(FALSE);

    UNREFERENCED_PARAMETER(bSuccess);

    if (!Registration || !(ReadAcquire(&Registration->State) & BASE_WER_RECOVERY_IN_PROGRESS))
        return;

    NtTerminateProcess(NtCurrentProcess(), ReadAcquire(&Registration->RecoveryExitStatus));
}

static
DWORD
WINAPI
BasepRecoveryThread(
    _In_ PVOID Parameter)
{
    PBASE_RECOVERY_RUN Run = Parameter;

    return Run->Callback(Run->Parameter);
}

VOID
WINAPI
BasepRunApplicationRecovery(
    _In_ NTSTATUS ExitStatus)
{
    PBASE_WER_REGISTRATION Registration = BaseWerGetRegistration(FALSE);
    LARGE_INTEGER Timeout;
    BASE_RECOVERY_RUN Run;
    HANDLE Handles[2];
    NTSTATUS Status;

    if (!Registration)
        return;

    RtlAcquireSRWLockExclusive(&Registration->Lock);
    if (Registration->State & BASE_WER_RECOVERY_IN_PROGRESS)
    {
        RtlReleaseSRWLockExclusive(&Registration->Lock);
        Sleep(INFINITE);
        return;
    }

    if (!(Registration->State & BASE_WER_RECOVERY_REGISTERED) ||
        !NT_SUCCESS(NtCreateEvent(&Handles[0], EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE)))
    {
        RtlReleaseSRWLockExclusive(&Registration->Lock);
        return;
    }

    Run.Callback = (APPLICATION_RECOVERY_CALLBACK)(ULONG_PTR)Registration->RecoveryCallback;
    Run.Parameter = (PVOID)(ULONG_PTR)Registration->RecoveryParameter;
    Timeout.QuadPart = -(LONGLONG)Registration->RecoveryPingInterval * BASE_RECOVERY_UNITS_PER_MS;
    Registration->RecoveryPingEvent = Handles[0];
    Registration->RecoveryExitStatus = ExitStatus;
    Registration->RecoveryCancelled = FALSE;
    InterlockedOr(&Registration->State, BASE_WER_RECOVERY_IN_PROGRESS);
    RtlReleaseSRWLockExclusive(&Registration->Lock);

    /* TODO: no recovery progress UI on the crash path, so pbCancelled reports FALSE there */
    Handles[1] = CreateThread(NULL, 0, BasepRecoveryThread, &Run, 0, NULL);
    if (Handles[1])
    {
        do
        {
            Status = NtWaitForMultipleObjects(RTL_NUMBER_OF(Handles), Handles, WaitAny, FALSE, &Timeout);
        } while (Status == STATUS_WAIT_0);
    }

    NtTerminateProcess(NtCurrentProcess(), ExitStatus);
}
