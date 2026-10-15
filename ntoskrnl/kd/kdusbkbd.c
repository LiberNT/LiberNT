/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB boot keyboard input for the kernel debugger during a bug check
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/drivers/dumpstor.h>
#include "kd.h"
#include "kdterminal.h"

#define KDP_USB_KEYBOARD_MAX            4
#define KDP_USB_KEY_QUEUE_SIZE          16
#define KDP_USB_KEY_SHIFTED             0x80
#define KDP_USB_MODIFIER_SHIFT          0x22
#define KDP_USB_FIRST_KEY               2
#define KDP_USB_ERROR_ROLLOVER          0x01
#define KDP_USB_FIRST_USAGE             0x04

typedef struct _KDP_USB_KEYBOARD
{
    volatile LONG Valid;
    BOOLEAN Prepared;
    ROS_USB_KEYBOARD_INTERFACE Interface;
    UCHAR LastReport[ROS_USB_KEYBOARD_BOOT_REPORT_SIZE];
} KDP_USB_KEYBOARD, *PKDP_USB_KEYBOARD;

static const UCHAR KdpUsbUsageToScanCode[] =
{
    0x00, 0x00, 0x00, 0x00, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
    0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x1c, 0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a,
    0x1b, 0x2b, 0x2b, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40,
    0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0x00, 0x46, 0x45, 0x52, 0x47, 0x49, 0x53, 0x4f, 0x51, 0x4d,
    0x4b, 0x50, 0x48, 0x45, 0x35, 0x37, 0x4a, 0x4e, 0x1c, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47,
    0x48, 0x49, 0x52, 0x53
};

static KDP_USB_KEYBOARD KdpUsbKeyboards[KDP_USB_KEYBOARD_MAX];
static KSPIN_LOCK KdpUsbKeyboardLock;
static PCALLBACK_OBJECT KdpUsbKeyboardCallback;
static UCHAR KdpUsbKeyQueue[KDP_USB_KEY_QUEUE_SIZE];
static ULONG KdpUsbKeyQueueHead;
static ULONG KdpUsbKeyQueueTail;

static VOID
NTAPI
KdpUsbKeyboardNotify(
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PROS_USB_KEYBOARD_INTERFACE Interface = Argument1;
    PKDP_USB_KEYBOARD Keyboard;
    KIRQL OldIrql;
    ULONG i;

    UNREFERENCED_PARAMETER(CallbackContext);

    if ((Interface == NULL) ||
        (Interface->Version != ROS_USB_KEYBOARD_INTERFACE_VERSION) ||
        (Interface->Size < sizeof(*Interface)) ||
        (Interface->Context == NULL) ||
        (Interface->Prepare == NULL) ||
        (Interface->Poll == NULL))
    {
        return;
    }

    KeAcquireSpinLock(&KdpUsbKeyboardLock, &OldIrql);
    for (i = 0; i < KDP_USB_KEYBOARD_MAX; i++)
    {
        Keyboard = &KdpUsbKeyboards[i];
        if (Argument2 != NULL)
        {
            if (Keyboard->Valid)
                continue;

            Keyboard->Interface = *Interface;
            Keyboard->Prepared = FALSE;
            RtlZeroMemory(Keyboard->LastReport, sizeof(Keyboard->LastReport));
            InterlockedExchange(&Keyboard->Valid, TRUE);
            break;
        }

        if (Keyboard->Valid && (Keyboard->Interface.Context == Interface->Context))
        {
            InterlockedExchange(&Keyboard->Valid, FALSE);
            break;
        }
    }
    KeReleaseSpinLock(&KdpUsbKeyboardLock, OldIrql);
}

VOID
KdpUsbKeyboardInitialize(VOID)
{
    UNICODE_STRING CallbackName = RTL_CONSTANT_STRING(ROS_USB_KEYBOARD_CALLBACK_NAME);
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    if (KdpUsbKeyboardCallback != NULL)
        return;

    KeInitializeSpinLock(&KdpUsbKeyboardLock);
    InitializeObjectAttributes(&ObjectAttributes, &CallbackName, OBJ_CASE_INSENSITIVE | OBJ_PERMANENT, NULL, NULL);
    Status = ExCreateCallback(&KdpUsbKeyboardCallback, &ObjectAttributes, TRUE, TRUE);
    if (!NT_SUCCESS(Status))
    {
        KdpUsbKeyboardCallback = NULL;
        return;
    }

    if (ExRegisterCallback(KdpUsbKeyboardCallback, KdpUsbKeyboardNotify, NULL) == NULL)
    {
        ObDereferenceObject(KdpUsbKeyboardCallback);
        KdpUsbKeyboardCallback = NULL;
    }
}

static VOID
KdpUsbKeyboardQueueKey(
    _In_ UCHAR Key)
{
    ULONG Next = (KdpUsbKeyQueueTail + 1) % KDP_USB_KEY_QUEUE_SIZE;

    if (Next == KdpUsbKeyQueueHead)
        return;

    KdpUsbKeyQueue[KdpUsbKeyQueueTail] = Key;
    KdpUsbKeyQueueTail = Next;
}

static VOID
KdpUsbKeyboardQueueReport(
    _Inout_ PKDP_USB_KEYBOARD Keyboard,
    _In_reads_bytes_(ROS_USB_KEYBOARD_BOOT_REPORT_SIZE) PUCHAR Report)
{
    UCHAR Shifted;
    UCHAR Usage;
    ULONG i, j;

    if (Report[KDP_USB_FIRST_KEY] == KDP_USB_ERROR_ROLLOVER)
        return;

    Shifted = (Report[0] & KDP_USB_MODIFIER_SHIFT) ? KDP_USB_KEY_SHIFTED : 0;
    for (i = KDP_USB_FIRST_KEY; i < ROS_USB_KEYBOARD_BOOT_REPORT_SIZE; i++)
    {
        Usage = Report[i];
        if ((Usage < KDP_USB_FIRST_USAGE) || (Usage >= RTL_NUMBER_OF(KdpUsbUsageToScanCode)))
            continue;

        for (j = KDP_USB_FIRST_KEY; j < ROS_USB_KEYBOARD_BOOT_REPORT_SIZE; j++)
        {
            if (Keyboard->LastReport[j] == Usage)
                break;
        }

        if ((j == ROS_USB_KEYBOARD_BOOT_REPORT_SIZE) && (KdpUsbUsageToScanCode[Usage] != 0))
            KdpUsbKeyboardQueueKey(KdpUsbUsageToScanCode[Usage] | Shifted);
    }

    RtlCopyMemory(Keyboard->LastReport, Report, sizeof(Keyboard->LastReport));
}

static CHAR
KdpUsbKeyboardDequeue(
    _Out_ PULONG ScanCode)
{
    UCHAR Key;
    CHAR c;

    while (KdpUsbKeyQueueHead != KdpUsbKeyQueueTail)
    {
        Key = KdpUsbKeyQueue[KdpUsbKeyQueueHead];
        KdpUsbKeyQueueHead = (KdpUsbKeyQueueHead + 1) % KDP_USB_KEY_QUEUE_SIZE;

        c = KdpScanCodeToChar(Key & ~KDP_USB_KEY_SHIFTED, (Key & KDP_USB_KEY_SHIFTED) != 0);
        if (c != -1)
        {
            *ScanCode = Key & ~KDP_USB_KEY_SHIFTED;
            return c;
        }
    }

    return -1;
}

CHAR
KdpUsbKeyboardTryGetChar(
    _Out_ PULONG ScanCode)
{
    UCHAR Report[ROS_USB_KEYBOARD_BOOT_REPORT_SIZE];
    PKDP_USB_KEYBOARD Keyboard;
    ULONG Length;
    NTSTATUS Status;
    ULONG i;
    CHAR c;

    if (!KeBugCheckActive)
        return -1;

    c = KdpUsbKeyboardDequeue(ScanCode);
    if (c != -1)
        return c;

    for (i = 0; i < KDP_USB_KEYBOARD_MAX; i++)
    {
        Keyboard = &KdpUsbKeyboards[i];
        if (!InterlockedCompareExchange(&Keyboard->Valid, 0, 0))
            continue;

        if (!Keyboard->Prepared)
        {
            Keyboard->Prepared = TRUE;
            Keyboard->Interface.Prepare(Keyboard->Interface.Context);
        }

        RtlZeroMemory(Report, sizeof(Report));
        Status = Keyboard->Interface.Poll(Keyboard->Interface.Context, Report, sizeof(Report), &Length);
        if ((Status == STATUS_SUCCESS) && (Length > KDP_USB_FIRST_KEY))
            KdpUsbKeyboardQueueReport(Keyboard, Report);
    }

    return KdpUsbKeyboardDequeue(ScanCode);
}

/* EOF */
