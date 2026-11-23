/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Drain wrapped NVNet receive descriptors after a single acknowledged interrupt
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rx-constants.h"

typedef uint32_t ULONG;
typedef uint16_t USHORT;
typedef unsigned char *PUCHAR;
typedef void *PVOID, *NDIS_HANDLE;
typedef unsigned char ETH_HEADER[14];
typedef struct { ULONG Length; PVOID Mdl; ULONG Offset, DataOffset; } BUFFER, *PNET_BUFFER;
typedef struct NBL {
    struct NBL *Next;
    BUFFER Buffer;
    PVOID MiniportReserved[2];
    ULONG Status;
} NBL, *PNET_BUFFER_LIST;
typedef struct { ULONG FlagsLength; } DESC;
typedef union { DESC *x32, *x64; } NV_RBD;
typedef struct ADAPTER {
    ULONG Flags, InterruptStatus, Features, CurrentRx, PacketFilter, Paused;
    ULONG ReceiveNblsOutstanding, OptimizationMode;
    PVOID AdapterHandle;
    struct { int Lock; NV_RBD NvRbd; } Receive;
    struct { int Lock; } Send;
    struct {
        ULONG ReceiveNoBuffers, ReceiveErrors, ReceiveIrqNoBuffers, ReceiveOk;
        ULONG ReceiveAlignmentErrors, ReceiveOverrunErrors, ReceiveCrcErrors;
    } Statistics;
    PUCHAR ReceiveBuffer;
    NBL *FreeReceiveNbls;
    ULONG (*ProcessTransmit)(struct ADAPTER *, NBL **, NBL **);
} ADAPTER, *PNVNET_ADAPTER;

#ifndef _In_
#define _In_
#endif
#define TRUE 1
#define NV_ACTIVE 1
#define DEV_HAS_HIGH_DMA 8
#define DEV_HAS_LARGEDESC 4
#define NV_OPTIMIZATION_MODE_DYNAMIC 2
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define NDIS_DbgPrint(level, args) ((void)0)
#define NdisDprAcquireSpinLock(lock) ((void)0)
#define NdisDprReleaseSpinLock(lock) ((void)0)
#define HandleLengthError(p, length) 0
#define NET_BUFFER_LIST_NEXT_NBL(n) ((n)->Next)
#define NET_BUFFER_LIST_FIRST_NB(n) (&(n)->Buffer)
#define NET_BUFFER_FIRST_MDL(n) ((n)->Mdl)
#define NET_BUFFER_CURRENT_MDL(n) ((n)->Mdl)
#define NET_BUFFER_CURRENT_MDL_OFFSET(n) ((n)->Offset)
#define NET_BUFFER_DATA_OFFSET(n) ((n)->DataOffset)
#define NET_BUFFER_DATA_LENGTH(n) ((n)->Length)
#define NET_BUFFER_LIST_STATUS(n) ((n)->Status)
#define NDIS_STATUS_SUCCESS 0
#define NdisMoveMemory memcpy
#define NdisAdjustMdlLength(m, len) ((void)0)
#define NvNetProcessSendQueue(a, h, t) ((void)0)
#define NvNetCompleteSends(a, h, f) ((void)0)
#define ChangeInterruptMode(a, w) ((void)0)
#define HandleLinkStateChange(a) ((void)0)
#define HandleRecoverableError(a) ((void)0)
#define NvNetApplyInterruptMask(a) (++Rearms)
#define NdisMIndicateReceiveNetBufferLists(a, head, port, count, flags) Indicate(a, head, count)

static unsigned Rearms, Indications, Delivered;

static void Indicate(ADAPTER *Adapter, NBL *Head, ULONG Count)
{
    assert(!Rearms && Count && Count <= NVNET_RECEIVE_PROCESSING_LIMIT);
    ++Indications;
    while (Head)
    {
        NBL *Next = Head->Next;
        assert(Count && Head->Buffer.Length == sizeof(ETH_HEADER));
        ULONG Sequence;
        memcpy(&Sequence, Head->MiniportReserved[0], sizeof(Sequence));
        assert(Sequence == Delivered);
        ++Delivered;
        --Count;
        --Adapter->ReceiveNblsOutstanding;
        Head->Next = Adapter->FreeReceiveNbls;
        Adapter->FreeReceiveNbls = Head;
        Head = Next;
    }
    assert(!Count);
}

static ULONG CompleteTx(ADAPTER *Adapter, NBL **Head, NBL **Tail) { return 0; }

#include "rx-routines.h"

static int Check(ULONG Burst, ULONG Features)
{
    static unsigned char Frames[NVNET_RECEIVE_DESCRIPTORS * NVNET_RECEIVE_BUFFER_SIZE];
    DESC Ring[NVNET_RECEIVE_DESCRIPTORS];
    NBL Lists[NVNET_RECEIVE_NBL_COUNT] = {0};
    ETH_HEADER Copies[NVNET_RECEIVE_NBL_COUNT];
    ADAPTER Adapter = {0};
    ULONG First = NVNET_RECEIVE_DESCRIPTORS - 2;
    ULONG Valid = 0;
    Adapter.CurrentRx = First;
    Adapter.AdapterHandle = &Adapter;
    Adapter.Flags = NV_ACTIVE;
    Adapter.PacketFilter = TRUE;
    Adapter.ProcessTransmit = CompleteTx;
    Adapter.ReceiveBuffer = Frames;
    Adapter.Features = Features;
    if (Features & DEV_HAS_HIGH_DMA)
        Adapter.Receive.NvRbd.x64 = Ring;
    else
        Adapter.Receive.NvRbd.x32 = Ring;
    for (ULONG i = 0; i < NVNET_RECEIVE_NBL_COUNT; ++i)
    {
        Lists[i].MiniportReserved[0] = Copies[i];
        Lists[i].Next = Adapter.FreeReceiveNbls;
        Adapter.FreeReceiveNbls = &Lists[i];
    }
    for (ULONG i = 0; i < NVNET_RECEIVE_DESCRIPTORS; ++i)
    {
        ULONG Slot = (First + i) % NVNET_RECEIVE_DESCRIPTORS;
        Ring[Slot].FlagsLength = (ULONG)NV_RX_AVAIL;
        if (i >= Burst)
            continue;
        Ring[Slot].FlagsLength = 0;
        if (i == 2)
            continue;
        Ring[Slot].FlagsLength = (Features ? NV_RX2_DESCRIPTORVALID : NV_RX_DESCRIPTORVALID) | sizeof(ETH_HEADER);
        memcpy(&Frames[Slot * NVNET_RECEIVE_BUFFER_SIZE], &Valid, sizeof(Valid));
        ++Valid;
    }
    Rearms = Indications = Delivered = 0;
    MiniportInterruptDpc(&Adapter, NULL, NULL, NULL);
    if (Delivered != Valid || Adapter.CurrentRx != (First + Burst) % NVNET_RECEIVE_DESCRIPTORS)
    {
        fprintf(stderr, "%u descriptors: delivered %u of %u, next descriptor %u\n",
                Burst, Delivered, Valid, Adapter.CurrentRx);
        return 1;
    }
    assert(Rearms == 1 && !Adapter.ReceiveNblsOutstanding);
    for (ULONG i = 0; i < NVNET_RECEIVE_DESCRIPTORS; ++i)
        assert(Ring[i].FlagsLength & NV_RX_AVAIL);
    return 0;
}

int main(void)
{
    int Errors = Check(0, 0);
    Errors += Check(64, 0);
    Errors += Check(65, DEV_HAS_LARGEDESC);
    Errors += Check(NVNET_RECEIVE_DESCRIPTORS, DEV_HAS_HIGH_DMA);
    return !!Errors;
}
