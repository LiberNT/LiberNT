/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Debug log broadcast over UDP through the UEFI Simple Network Protocol
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>

#include "uefinetp.h"

#define UDPLOG_MAX_ADAPTERS  4
#define UDPLOG_TX_BUFFERS    2
#define UDPLOG_FRAME_BYTES   2048
#define UDPLOG_PAYLOAD       1024
#define UDPLOG_QUEUE_BYTES   (64 * 1024)
#define UDPLOG_MAC_BYTES     6
#define UDPLOG_ETH_HEADER    14
#define UDPLOG_IP_HEADER     20
#define UDPLOG_UDP_HEADER    8
#define UDPLOG_ETH_MIN_FRAME 60
#define UDPLOG_ETHERTYPE_IP  0x0800
#define UDPLOG_IP_TTL        64
#define UDPLOG_IP_UDP        17
#define UDPLOG_SOURCE_PORT   6665
#define UDPLOG_TARGET_PORT   6666
#define UDPLOG_SEND_ATTEMPTS 1000
#define UDPLOG_RECYCLE_POLLS 10000

typedef struct _UDPLOG_ADAPTER
{
    EFI_SIMPLE_NETWORK_PROTOCOL* Snp;
    UINT32 InitialState;
    UCHAR HardwareMac[UDPLOG_MAC_BYTES];
    UCHAR Mac[UDPLOG_MAC_BYTES];
    PUCHAR Frames;
    BOOLEAN Busy[UDPLOG_TX_BUFFERS];
} UDPLOG_ADAPTER, *PUDPLOG_ADAPTER;

static EFI_GUID UdpLogSnpGuid = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
static UDPLOG_ADAPTER UdpLogAdapters[UDPLOG_MAX_ADAPTERS];
static ULONG UdpLogAdapterCount;
static BOOLEAN UdpLogOpened;
static BOOLEAN UdpLogActive;
static USHORT UdpLogIpId;
static CHAR UdpLogQueue[UDPLOG_QUEUE_BYTES];
static ULONG UdpLogQueued;

VOID (*UdpLogForeignRecycle)(EFI_SIMPLE_NETWORK_PROTOCOL* Snp, VOID* Buffer);

static
BOOLEAN
UdpLogEnter(
    _Out_ EFI_TPL* OldTpl)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;

    *OldTpl = BootServices->RaiseTPL(TPL_HIGH_LEVEL);
    BootServices->RestoreTPL(*OldTpl);
    if (*OldTpl > TPL_CALLBACK)
        return FALSE;

    *OldTpl = BootServices->RaiseTPL(TPL_CALLBACK);
    return TRUE;
}

static
VOID
UdpLogPut16(
    _Out_writes_bytes_(2) PUCHAR Buffer,
    _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)(Value >> 8);
    Buffer[1] = (UCHAR)Value;
}

static
VOID
UdpLogStoreIpChecksum(
    _Inout_updates_bytes_(UDPLOG_IP_HEADER) PUCHAR Ip)
{
    ULONG Sum = 0, i;

    for (i = 0; i < UDPLOG_IP_HEADER; i += 2)
        Sum += ((ULONG)Ip[i] << 8) | Ip[i + 1];
    while (Sum >> 16)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);

    UdpLogPut16(Ip + 10, ~Sum & 0xFFFF);
}

static
BOOLEAN
UdpLogLinkUp(
    _In_ PUDPLOG_ADAPTER Adapter)
{
    if (!Adapter->Snp->Mode->MediaPresentSupported)
        return TRUE;

    Adapter->Snp->GetStatus(Adapter->Snp, NULL, NULL);
    return Adapter->Snp->Mode->MediaPresent;
}

static
VOID
UdpLogRecycle(
    _In_ PUDPLOG_ADAPTER Adapter)
{
    VOID* Done;
    ULONG i;

    for (;;)
    {
        Done = NULL;
        if (EFI_ERROR(Adapter->Snp->GetStatus(Adapter->Snp, NULL, &Done)) || !Done)
            return;

        for (i = 0; i < UDPLOG_TX_BUFFERS && Done != Adapter->Frames + i * UDPLOG_FRAME_BYTES; i++)
        {
        }

        if (i < UDPLOG_TX_BUFFERS)
            Adapter->Busy[i] = FALSE;
        /* FIXME: a recycled transmit buffer of the firmware MNP is dropped here, so MNP never reuses it */
        else if (UdpLogForeignRecycle)
            UdpLogForeignRecycle(Adapter->Snp, Done);
    }
}

BOOLEAN
UdpLogRecycleForeign(
    _In_ EFI_SIMPLE_NETWORK_PROTOCOL* Snp,
    _In_ VOID* Buffer)
{
    ULONG Adapter, i;

    for (Adapter = 0; Adapter < UdpLogAdapterCount; Adapter++)
    {
        if (UdpLogAdapters[Adapter].Snp != Snp)
            continue;

        for (i = 0; i < UDPLOG_TX_BUFFERS; i++)
        {
            if (Buffer == UdpLogAdapters[Adapter].Frames + i * UDPLOG_FRAME_BYTES)
            {
                UdpLogAdapters[Adapter].Busy[i] = FALSE;
                return TRUE;
            }
        }
    }

    return FALSE;
}

static
VOID
UdpLogSend(
    _In_ PUDPLOG_ADAPTER Adapter,
    _In_reads_bytes_(Length) PCSTR Payload,
    _In_ ULONG Length)
{
    EFI_STATUS Status = EFI_NOT_READY;
    PUCHAR Frame, Ip, Udp;
    ULONG FrameLength, Attempt, i;

    for (i = 0; i < UDPLOG_TX_BUFFERS && Adapter->Busy[i]; i++)
    {
    }

    if (i == UDPLOG_TX_BUFFERS)
    {
        UdpLogRecycle(Adapter);
        for (i = 0; i < UDPLOG_TX_BUFFERS && Adapter->Busy[i]; i++)
        {
        }
        if (i == UDPLOG_TX_BUFFERS)
            return;
    }

    Frame = Adapter->Frames + i * UDPLOG_FRAME_BYTES;
    Ip = Frame + UDPLOG_ETH_HEADER;
    Udp = Ip + UDPLOG_IP_HEADER;

    RtlFillMemory(Frame, UDPLOG_MAC_BYTES, 0xFF);
    RtlCopyMemory(Frame + UDPLOG_MAC_BYTES, Adapter->Mac, UDPLOG_MAC_BYTES);
    UdpLogPut16(Frame + 12, UDPLOG_ETHERTYPE_IP);

    RtlZeroMemory(Ip, UDPLOG_IP_HEADER);
    Ip[0] = 0x45;
    UdpLogPut16(Ip + 2, UDPLOG_IP_HEADER + UDPLOG_UDP_HEADER + Length);
    UdpLogPut16(Ip + 4, UdpLogIpId++);
    Ip[8] = UDPLOG_IP_TTL;
    Ip[9] = UDPLOG_IP_UDP;
    RtlFillMemory(Ip + 16, 4, 0xFF);
    UdpLogStoreIpChecksum(Ip);

    UdpLogPut16(Udp, UDPLOG_SOURCE_PORT);
    UdpLogPut16(Udp + 2, UDPLOG_TARGET_PORT);
    UdpLogPut16(Udp + 4, UDPLOG_UDP_HEADER + Length);
    UdpLogPut16(Udp + 6, 0);
    RtlCopyMemory(Udp + UDPLOG_UDP_HEADER, Payload, Length);

    FrameLength = UDPLOG_ETH_HEADER + UDPLOG_IP_HEADER + UDPLOG_UDP_HEADER + Length;
    if (FrameLength < UDPLOG_ETH_MIN_FRAME)
    {
        RtlZeroMemory(Frame + FrameLength, UDPLOG_ETH_MIN_FRAME - FrameLength);
        FrameLength = UDPLOG_ETH_MIN_FRAME;
    }

    Adapter->Busy[i] = TRUE;
    for (Attempt = 0; Attempt < UDPLOG_SEND_ATTEMPTS && Status == EFI_NOT_READY; Attempt++)
    {
        Status = Adapter->Snp->Transmit(Adapter->Snp, 0, FrameLength, Frame, NULL, NULL, NULL);
        if (Status == EFI_NOT_READY)
            UdpLogRecycle(Adapter);
    }

    if (EFI_ERROR(Status))
    {
        Adapter->Busy[i] = FALSE;
        return;
    }

    for (Attempt = 0; Attempt < UDPLOG_RECYCLE_POLLS && Adapter->Busy[i]; Attempt++)
    {
        UdpLogRecycle(Adapter);
        if (Adapter->Busy[i])
            GlobalSystemTable->BootServices->Stall(1);
    }
}

static
VOID
UdpLogFlush(
    _In_ BOOLEAN All)
{
    BOOLEAN Linked[UDPLOG_MAX_ADAPTERS];
    BOOLEAN AnyLinked = FALSE;
    ULONG Chunk, Window, i;

    for (i = 0; i < UdpLogAdapterCount; i++)
    {
        Linked[i] = UdpLogLinkUp(&UdpLogAdapters[i]);
        AnyLinked |= Linked[i];
    }

    while (AnyLinked && UdpLogQueued)
    {
        Window = min(UdpLogQueued, UDPLOG_PAYLOAD);
        for (Chunk = Window; Chunk > 0 && UdpLogQueue[Chunk - 1] != '\n'; Chunk--)
        {
        }

        if (Chunk == 0)
        {
            if (!All && UdpLogQueued < UDPLOG_PAYLOAD)
                return;
            Chunk = Window;
        }

        for (i = 0; i < UdpLogAdapterCount; i++)
        {
            if (Linked[i])
                UdpLogSend(&UdpLogAdapters[i], UdpLogQueue, Chunk);
        }

        UdpLogQueued -= Chunk;
        RtlMoveMemory(UdpLogQueue, UdpLogQueue + Chunk, UdpLogQueued);
    }
}

ULONG
UdpLogOpen(VOID)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_SIMPLE_NETWORK_PROTOCOL* Snp;
    PUDPLOG_ADAPTER Adapter;
    EFI_HANDLE* Handles;
    EFI_TPL OldTpl;
    UINTN Count, i;
    ULONG j;

    if (!DBG || UdpLogOpened || BootServicesExitedFlag || !UdpLogEnter(&OldTpl))
        return UdpLogAdapterCount;

    UdpLogOpened = TRUE;
    UdpLogAdapterCount = 0;
    if (EFI_ERROR(BootServices->LocateHandleBuffer(ByProtocol, &UdpLogSnpGuid, NULL, &Count, &Handles)))
    {
        BootServices->RestoreTPL(OldTpl);
        return 0;
    }

    for (i = 0; i < Count && UdpLogAdapterCount < UDPLOG_MAX_ADAPTERS; i++)
    {
        if (EFI_ERROR(BootServices->HandleProtocol(Handles[i], &UdpLogSnpGuid, (VOID**)&Snp)) ||
            !Snp || !Snp->Mode ||
            Snp->Mode->HwAddressSize != UDPLOG_MAC_BYTES ||
            Snp->Mode->MediaHeaderSize != UDPLOG_ETH_HEADER)
        {
            continue;
        }

        for (j = 0; j < UdpLogAdapterCount; j++)
        {
            if (UdpLogAdapters[j].Snp == Snp ||
                RtlEqualMemory(UdpLogAdapters[j].HardwareMac, Snp->Mode->CurrentAddress.Addr, UDPLOG_MAC_BYTES))
            {
                break;
            }
        }
        if (j < UdpLogAdapterCount)
            continue;

        Adapter = &UdpLogAdapters[UdpLogAdapterCount];
        RtlZeroMemory(Adapter, sizeof(*Adapter));
        Adapter->Snp = Snp;
        RtlCopyMemory(Adapter->HardwareMac, Snp->Mode->CurrentAddress.Addr, UDPLOG_MAC_BYTES);
        Adapter->InitialState = Snp->Mode->State;

        if (Snp->Mode->State == EfiSimpleNetworkStopped)
            Snp->Start(Snp);
        if (Snp->Mode->State == EfiSimpleNetworkStarted)
            Snp->Initialize(Snp, 0, 0);

        if (Snp->Mode->State == EfiSimpleNetworkInitialized)
            Adapter->Frames = FwAllocatePages(UDPLOG_TX_BUFFERS * UDPLOG_FRAME_BYTES);

        if (!Adapter->Frames)
        {
            if (Adapter->InitialState != EfiSimpleNetworkInitialized && Snp->Mode->State == EfiSimpleNetworkInitialized)
                Snp->Shutdown(Snp);
            if (Adapter->InitialState == EfiSimpleNetworkStopped && Snp->Mode->State == EfiSimpleNetworkStarted)
                Snp->Stop(Snp);
            continue;
        }

        UefiLattePandaFixMac(Snp);
        RtlCopyMemory(Adapter->Mac, Snp->Mode->CurrentAddress.Addr, UDPLOG_MAC_BYTES);
        UdpLogIpId ^= (USHORT)((Adapter->Mac[4] << 8) | Adapter->Mac[5]);
        UdpLogAdapterCount++;
    }

    BootServices->FreePool(Handles);
    UdpLogFlush(FALSE);
    BootServices->RestoreTPL(OldTpl);
    return UdpLogAdapterCount;
}

VOID
UdpLogClose(VOID)
{
    PUDPLOG_ADAPTER Adapter;
    EFI_TPL OldTpl;
    ULONG i, Busy;

    if (!UdpLogOpened || BootServicesExitedFlag || !UdpLogEnter(&OldTpl))
        return;

    UdpLogActive = TRUE;
    UdpLogFlush(TRUE);
    for (i = 0; i < UdpLogAdapterCount; i++)
    {
        Adapter = &UdpLogAdapters[i];
        UdpLogRecycle(Adapter);

        if (Adapter->InitialState != EfiSimpleNetworkInitialized)
            Adapter->Snp->Shutdown(Adapter->Snp);
        if (Adapter->InitialState == EfiSimpleNetworkStopped)
            Adapter->Snp->Stop(Adapter->Snp);

        for (Busy = 0; Busy < UDPLOG_TX_BUFFERS && !Adapter->Busy[Busy]; Busy++)
        {
        }
        if (Busy == UDPLOG_TX_BUFFERS)
            FwFreePages(Adapter->Frames, UDPLOG_TX_BUFFERS * UDPLOG_FRAME_BYTES);
    }

    UdpLogAdapterCount = 0;
    UdpLogOpened = FALSE;
    UdpLogActive = FALSE;
    GlobalSystemTable->BootServices->RestoreTPL(OldTpl);
}

VOID
UdpLogWrite(
    _In_ PCSTR Text)
{
    SIZE_T Length;
    EFI_TPL OldTpl;

    if (!DBG || UdpLogActive || BootServicesExitedFlag)
        return;

    UdpLogActive = TRUE;
    Length = min(strlen(Text), (SIZE_T)(UDPLOG_QUEUE_BYTES - UdpLogQueued));
    RtlCopyMemory(UdpLogQueue + UdpLogQueued, Text, Length);
    UdpLogQueued += (ULONG)Length;

    if (UdpLogOpened && UdpLogEnter(&OldTpl))
    {
        UdpLogFlush(FALSE);
        GlobalSystemTable->BootServices->RestoreTPL(OldTpl);
    }
    UdpLogActive = FALSE;
}
