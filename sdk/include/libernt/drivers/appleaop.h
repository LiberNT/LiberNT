/*
 * PROJECT:     LiberNT Apple AOP Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interface the Apple Always-On Processor driver gives the functions it enumerates
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

DEFINE_GUID(GUID_APPLE_AOP_FUNCTION_INTERFACE,
            0x66625d46, 0x238f, 0x4420, 0xa5, 0x95, 0x19, 0x16, 0x04, 0xca, 0x22, 0xcd);

#define APPLE_AOP_FUNCTION_INTERFACE_VERSION 1

typedef enum _APPLE_AOP_FUNCTION
{
    AppleAopAmbientLight = 1,
    AppleAopLidAngle = 2,
    AppleAopMicrophones = 3
} APPLE_AOP_FUNCTION;

typedef VOID
(NTAPI *PAPPLE_AOP_SAMPLE_CALLBACK)(
    _In_ PVOID Context,
    _In_ ULONG Value);

typedef NTSTATUS
(NTAPI *PAPPLE_AOP_SUBSCRIBE)(
    _In_ PVOID Context,
    _In_opt_ PAPPLE_AOP_SAMPLE_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext);

typedef NTSTATUS
(NTAPI *PAPPLE_AOP_READ_SAMPLE)(
    _In_ PVOID Context,
    _Out_ PULONG Value);

typedef NTSTATUS
(NTAPI *PAPPLE_AOP_CALL)(
    _In_ PVOID Context,
    _In_ USHORT Subtype,
    _In_reads_bytes_(RequestLength) const VOID *Request,
    _In_ ULONG RequestLength,
    _Out_writes_bytes_opt_(ReplyLength) PVOID Reply,
    _In_ ULONG ReplyLength,
    _Out_ PULONG ReturnCode);

typedef struct _APPLE_AOP_FUNCTION_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    APPLE_AOP_FUNCTION Function;
    PAPPLE_AOP_SUBSCRIBE Subscribe;
    PAPPLE_AOP_READ_SAMPLE ReadSample;
    PAPPLE_AOP_CALL Call;
} APPLE_AOP_FUNCTION_INTERFACE, *PAPPLE_AOP_FUNCTION_INTERFACE;
