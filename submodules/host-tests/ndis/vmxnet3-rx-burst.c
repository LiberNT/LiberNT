/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercise VMXNET3 interrupt rearming after a coalesced receive burst
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include <stdint.h>
#include <stdio.h>
#include "rx-constants.h"

typedef uint32_t ULONG;
typedef void *NDIS_HANDLE, *PVOID;
typedef struct { ULONG MaxNblsToIndicate, MoreNblsPending; } THROTTLE, *PNDIS_RECEIVE_THROTTLE_PARAMETERS;
typedef struct { ULONG Ecr; } SHARED;
typedef struct { SHARED *Shared; int Activated; ULONG Pending, Mask; } ADAPTER, *PVMX3_ADAPTER;
#ifndef _In_
#define _In_
#endif
#define TRUE 1
#define FALSE 0
#define NDIS_INDICATE_ALL_NBLS UINT32_MAX
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define Vmx3ReceivePending(a) ((a)->Pending != 0)
#define Vmx3CompleteTransmits(a) ((void)(a))
#define Vmx3WriteVd(a, r, v) ((void)0)
#define Vmx3UpdateLink(a, v) ((void)0)
#define Vmx3WritePt(a, r, v) ((a)->Mask = (v))

static void Vmx3ProcessReceives(ADAPTER *Adapter, ULONG Budget)
{
    Adapter->Pending -= min(Adapter->Pending, Budget);
}

#include "rx-routines.h"

static int Check(ULONG Burst, THROTTLE *Throttle, ULONG ExpectedLeft)
{
    SHARED Shared = {0};
    ADAPTER Adapter = {&Shared, TRUE, Burst, 1};
    Vmx3InterruptDpc(&Adapter, NULL, Throttle, NULL);
    if (Adapter.Pending == ExpectedLeft && Adapter.Mask == !!ExpectedLeft &&
        (!Throttle || Throttle->MoreNblsPending == !!ExpectedLeft))
        return 0;
    fprintf(stderr, "%u queued, limit %u: left %u (expected %u), mask %u, more %u\n",
            Burst, Throttle ? Throttle->MaxNblsToIndicate : UINT32_MAX,
            Adapter.Pending, ExpectedLeft, Adapter.Mask, Throttle ? Throttle->MoreNblsPending : 0);
    return 1;
}

int main(void)
{
    THROTTLE All = {NDIS_INDICATE_ALL_NBLS, TRUE};
    THROTTLE Finite = {192, FALSE};
    THROTTLE Zero = {0, FALSE};
    ULONG Full = VMX3_RX_RING_SIZE - 1;
    int Errors = Check(0, &All, 0);
    Errors += Check(129, &All, 0);
    Errors += Check(Full, NULL, 0);
    Errors += Check(Full, &Finite, Full - Finite.MaxNblsToIndicate);
    Errors += Check(Full - Finite.MaxNblsToIndicate, &Finite, 0);
    Errors += Check(1, &Zero, 1);
    return !!Errors;
}
