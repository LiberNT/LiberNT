/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Network terminal of the kernel debugger, served by the transport module
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"
#include <kdterm.h>

static KD_TERMINAL_INTERFACE KdpNetInterface;
static const KD_TERMINAL_HOST KdpNetHost = {KdpInfoQuery, KdpInfoState};
static KSPIN_LOCK KdpNetSpinLock;
static volatile BOOLEAN KdpNetReady;
static BOOLEAN KdpNetConfigured;
static BOOLEAN KdpNetPrinted;

static
VOID
NTAPI
KdpNetPrint(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    BOOLEAN LockAcquired;
    KIRQL OldIrql;

    LockAcquired = KdbpAcquireLock(&KdpNetSpinLock, &OldIrql);
    if (LockAcquired)
    {
        KdpNetInterface.Write(String, Length);
        KdpNetPrinted = TRUE;
    }

    KdbpReleaseLock(&KdpNetSpinLock, OldIrql, LockAcquired);
}

VOID
KdpNetSendCommand(
    _In_ PCSTR Command)
{
    KdpNetPrint(Command, (ULONG)strlen(Command));
}

BOOLEAN
KdpNetGetByte(
    _Out_ PUCHAR Byte)
{
    return KdpNetReady && KdpNetInterface.Read(Byte);
}

BOOLEAN
KdpNetPollBreakIn(VOID)
{
    if (!KdpNetReady)
        return FALSE;

    if (KdpNetPrinted)
    {
        KdpNetPrinted = FALSE;
        return FALSE;
    }

    return KdpNetInterface.Poll();
}

BOOLEAN
KdpNetQueryAttached(
    _Out_opt_ PULONG Columns,
    _Out_opt_ PULONG Rows)
{
    return KdpNetReady && KdpNetInterface.QueryAttached(Columns, Rows);
}

VOID
KdpNetDisplayStatus(VOID)
{
    CHAR Text[160];

    if (!KdQueryTransportStatus(Text, sizeof(Text)))
        return;

    HalDisplayString("   ");
    HalDisplayString(Text);
    HalDisplayString("\r\n");
    if (KdQueryTransportKey(Text, sizeof(Text)))
    {
        HalDisplayString("   Network debugger key: ");
        HalDisplayString(Text);
        HalDisplayString("\r\n");
    }
}

ULONG
NTAPI
KdQueryTransportStatus(
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ ULONG Size)
{
    if (!Size)
        return 0;

    *Buffer = ANSI_NULL;
    return KdpNetReady ? KdpNetInterface.QueryStatus(Buffer, Size) : 0;
}

ULONG
NTAPI
KdQueryTransportKey(
    _Out_writes_z_(Size) PCHAR Buffer,
    _In_ ULONG Size)
{
    if (!Size)
        return 0;

    *Buffer = ANSI_NULL;
    return KdpNetConfigured ? KdpNetInterface.QueryKey(Buffer, Size) : 0;
}

BOOLEAN
NTAPI
KdDisplayTransportKey(
    _In_ BOOLEAN DarkPalette)
{
    static const CHAR Label[] = "Network assistance key ";
    CHAR Text[sizeof(Label) + 40];
    VID_DISPLAY_INFO DisplayInfo;

    RtlCopyMemory(Text, Label, sizeof(Label));
    if (!KdQueryTransportKey(Text + sizeof(Label) - 1, sizeof(Text) - sizeof(Label) + 1))
        return FALSE;

    if (!InbvIsBootDriverInstalled() || InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED)
        return FALSE;

    if (DarkPalette)
    {
        InbvQueryDisplayInfo(&DisplayInfo);
        InbvSolidColorFill(0, 0, (ULONG)strlen(Text) * DisplayInfo.CharacterWidth - 1,
                           DisplayInfo.CharacterHeight - 1, BV_COLOR_BLACK);
    }

    InbvAcquireLock();
    VidDisplayStringXY(Text, 0, 0, DarkPalette);
    InbvReleaseLock();
    return TRUE;
}

static
VOID
KdpNetPublishKey(VOID)
{
    static UNICODE_STRING KeyName = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\NetworkAssistance");
    static UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Key");
    SECURITY_DESCRIPTOR SecurityDescriptor;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UCHAR DaclBuffer[128];
    PACL Dacl = (PACL)DaclBuffer;
    WCHAR Text[40];
    CHAR Key[40];
    HANDLE Handle;
    ULONG Length, i;

    Length = KdQueryTransportKey(Key, sizeof(Key));
    if (!Length)
        return;

    for (i = 0; i <= Length; i++)
        Text[i] = (UCHAR)Key[i];

    if (!NT_SUCCESS(RtlCreateAcl(Dacl, sizeof(DaclBuffer), ACL_REVISION)) ||
        !NT_SUCCESS(RtlAddAccessAllowedAce(Dacl, ACL_REVISION, KEY_ALL_ACCESS, SeLocalSystemSid)) ||
        !NT_SUCCESS(RtlAddAccessAllowedAce(Dacl, ACL_REVISION, KEY_ALL_ACCESS, SeAliasAdminsSid)) ||
        !NT_SUCCESS(RtlAddAccessAllowedAce(Dacl, ACL_REVISION, KEY_READ, SeInteractiveSid)) ||
        !NT_SUCCESS(RtlCreateSecurityDescriptor(&SecurityDescriptor, SECURITY_DESCRIPTOR_REVISION)) ||
        !NT_SUCCESS(RtlSetDaclSecurityDescriptor(&SecurityDescriptor, TRUE, Dacl, FALSE)))
    {
        return;
    }

    SecurityDescriptor.Control |= SE_DACL_PROTECTED;
    InitializeObjectAttributes(&ObjectAttributes, &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, &SecurityDescriptor);
    if (!NT_SUCCESS(ZwCreateKey(&Handle, KEY_SET_VALUE, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL)))
        return;

    ZwSetValueKey(Handle, &ValueName, 0, REG_SZ, Text, (Length + 1) * sizeof(WCHAR));
    ZwClose(Handle);
}

NTSTATUS
NTAPI
KdpNetInit(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    CHAR Status[192];
    NTSTATUS Result;

    if (!KdpDebugMode.Net)
        return STATUS_PORT_DISCONNECTED;

    if (BootPhase == 0)
    {
        Result = KdQueryTerminalInterface(&KdpNetInterface);
        if (!NT_SUCCESS(Result) || KdpNetInterface.Version != KD_TERMINAL_INTERFACE_VERSION)
        {
            KdpDebugMode.Net = FALSE;
            return STATUS_DEVICE_DOES_NOT_EXIST;
        }

        KdpNetInterface.Configure(KeLoaderBlock ? KeLoaderBlock->LoadOptions : NULL);
        KdpNetInterface.SetHost(&KdpNetHost);
        KdpNetConfigured = TRUE;
        KeInitializeSpinLock(&KdpNetSpinLock);
        DispatchTable->KdpPrintRoutine = KdpNetPrint;
        DispatchTable->KdpInitRoutine = KdpNetInit;
        InsertTailList(&KdProviders, &DispatchTable->KdProvidersList);
    }
    else if (BootPhase == 1)
    {
        Result = KdpNetInterface.Initialize(KeLoaderBlock);
        if (!NT_SUCCESS(Result))
        {
            KdpDebugMode.Net = FALSE;
            KdpNetConfigured = FALSE;
            HalDisplayString("   Network debugging unavailable\r\n");
            DbgPrint("Network debugger: no usable network card, status 0x%08lx\n", Result);
            return Result;
        }

        KdpInfoInitialize(KeLoaderBlock);
        KeMemoryBarrier();
        KdpNetReady = TRUE;
        HalDisplayString("   Network debugging enabled\r\n");
        KdpNetDisplayStatus();
        KdDisplayTransportKey(SharedUserData->NtProductType == NtProductWinNt &&
                              strstr(KeLoaderBlock->LoadOptions, "SOS") &&
                              !strstr(KeLoaderBlock->LoadOptions, "NOGUIBOOT"));
        KdpNetPublishKey();
        KdQueryTransportStatus(Status, sizeof(Status));
        DbgPrint("%s\n", Status);
    }

    return STATUS_SUCCESS;
}
