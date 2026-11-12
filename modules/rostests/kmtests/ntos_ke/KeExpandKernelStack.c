/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a callout on an expanded kernel stack takes page faults wherever that stack lies
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define CALLOUT_DEPTH 6

typedef struct _STACK_CALLOUT
{
    volatile UCHAR *Pages;
    ULONG_PTR OuterLow;
    ULONG_PTR OuterHigh;
    ULONG Level;
    ULONG Moved;
    ULONG Above;
    NTSTATUS Status;
} STACK_CALLOUT, *PSTACK_CALLOUT;

static
VOID
NTAPI
TouchOnNewStack(
    _In_ PVOID Parameter)
{
    PSTACK_CALLOUT Callout = Parameter;
    volatile UCHAR Fill[PAGE_SIZE];
    ULONG Level = Callout->Level;
    ULONG_PTR Low, High;
    NTSTATUS Status;

    IoGetStackLimits(&Low, &High);
    if ((High <= Callout->OuterLow) || (Low >= Callout->OuterHigh)) Callout->Moved++;
    if (Low >= Callout->OuterHigh) Callout->Above++;

    Callout->Pages[Level * PAGE_SIZE] = (UCHAR)(Level + 1);

    RtlFillMemory((PVOID)Fill, sizeof(Fill), (UCHAR)Level);
    if (Level + 1 == CALLOUT_DEPTH) return;

    Callout->Level = Level + 1;
    Status = KeExpandKernelStackAndCallout(TouchOnNewStack, Callout, MAXIMUM_EXPANSION_SIZE);
    if (!NT_SUCCESS(Status)) Callout->Status = Status;
    ok_eq_uint(Fill[Level], Level);
}

START_TEST(KeExpandKernelStack)
{
    SIZE_T Size = CALLOUT_DEPTH * PAGE_SIZE;
    STACK_CALLOUT Callout;
    PVOID Base = NULL;
    NTSTATUS Status;
    ULONG Level;

    Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &Base, 0, &Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    RtlZeroMemory(&Callout, sizeof(Callout));
    Callout.Pages = Base;
    Callout.Status = STATUS_SUCCESS;
    IoGetStackLimits(&Callout.OuterLow, &Callout.OuterHigh);

    Status = KeExpandKernelStackAndCallout(TouchOnNewStack, &Callout, MAXIMUM_EXPANSION_SIZE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Callout.Status, STATUS_SUCCESS);
    ok(Callout.Moved != 0, "No callout ran on a stack of its own\n");

    for (Level = 0; Level < CALLOUT_DEPTH; Level++)
    {
        ok(Callout.Pages[Level * PAGE_SIZE] == (UCHAR)(Level + 1),
           "Callout %lu wrote 0x%x\n", Level, Callout.Pages[Level * PAGE_SIZE]);
    }

    trace("%lu of %u callout stacks lay above the thread's own\n", Callout.Above, CALLOUT_DEPTH);
    skip(Callout.Above != 0, "No callout stack lay above the thread's own\n");

    Size = 0;
    Status = ZwFreeVirtualMemory(ZwCurrentProcess(), &Base, &Size, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}
