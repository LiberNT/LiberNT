/*
 * PROJECT:     LiberNT i8042 port driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Rules for the callbacks a keyboard filter hooks into the port
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

NTSTATUS
I8042CoreKeyboardInitialization(
    _In_ PI8042_KEYBOARD_INITIALIZATION_ROUTINE InitializationRoutine,
    _In_ PVOID InitializationContext,
    _In_ PVOID SynchFuncContext,
    _In_ PI8042_SYNCH_READ_PORT ReadPort,
    _In_ PI8042_SYNCH_WRITE_PORT WritePort,
    _Out_ PBOOLEAN TurnTranslationOn);

NTSTATUS
I8042CoreSendStartInformation(
    _In_ PDEVICE_OBJECT TopOfStack,
    _In_ ULONG IoControlCode,
    _In_ PKINTERRUPT InterruptObject);
