/*
 */

#include <ntoskrnl.h>

#undef KeAcquireSpinLock

KIRQL
FASTCALL
KfAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock)
{
}

VOID
FASTCALL
KfReleaseSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
}

VOID
NTAPI
KeAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql)
{
}
