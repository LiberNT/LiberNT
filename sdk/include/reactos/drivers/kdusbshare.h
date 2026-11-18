/*
 * PROJECT:     LiberNT USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     xHCI controller shared between the USB network debugger and the OS driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define KD_USB_SHARE_SIGNATURE      'SBUK'
#define KD_USB_SHARE_MAX_SLOTS      4
#define KD_USB_SHARE_VALUE          L"Share"
#define KD_USB_SHARE_KEY_PATH       L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\usbxhci\\Debug"

typedef struct _KD_USB_SHARE
{
    ULONG Signature;
    ULONG Size;
    ULONG Port;
    ULONG Interrupter;
    ULONG Phone;
    ULONG SlotCount;
    UCHAR Slots[KD_USB_SHARE_MAX_SLOTS];
    ULONG64 DeviceContexts[KD_USB_SHARE_MAX_SLOTS];
    ULONG64 ContextAddresses[KD_USB_SHARE_MAX_SLOTS];
    ULONG Endpoints[KD_USB_SHARE_MAX_SLOTS];
    ULONG64 Dcbaa;
    ULONG64 ScratchpadArray;
    ULONG64 Registers;
} KD_USB_SHARE, *PKD_USB_SHARE;

FORCEINLINE
NTSTATUS
KdUsbShareRead(
    _Out_ PKD_USB_SHARE Share)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(KD_USB_SHARE_KEY_PATH);
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(KD_USB_SHARE_VALUE);
    ULONG Buffer[(FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(KD_USB_SHARE) + sizeof(ULONG) - 1) /
                 sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Length;
    HANDLE Key;
    NTSTATUS Status;

    RtlZeroMemory(Share, sizeof(*Share));
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, sizeof(Buffer), &Length);
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Value->Type != REG_BINARY || Value->DataLength != sizeof(KD_USB_SHARE))
        return STATUS_OBJECT_TYPE_MISMATCH;
    RtlCopyMemory(Share, Value->Data, sizeof(*Share));
    if (Share->Signature != KD_USB_SHARE_SIGNATURE || Share->Size != sizeof(*Share) ||
        Share->SlotCount > KD_USB_SHARE_MAX_SLOTS)
    {
        return STATUS_REVISION_MISMATCH;
    }
    return STATUS_SUCCESS;
}
