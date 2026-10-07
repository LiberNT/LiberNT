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
    return KdpNetReady ? KdpNetInterface.QueryKey(Buffer, Size) : 0;
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
            HalDisplayString("   Network debugging unavailable\r\n");
            DbgPrint("Network debugger: no usable network card, status 0x%08lx\n", Result);
            return Result;
        }

        KdpInfoInitialize(KeLoaderBlock);
        KeMemoryBarrier();
        KdpNetReady = TRUE;
        HalDisplayString("   Network debugging enabled\r\n");
        KdpNetDisplayStatus();
        KdQueryTransportStatus(Status, sizeof(Status));
        DbgPrint("%s\n", Status);
    }

    return STATUS_SUCCESS;
}
