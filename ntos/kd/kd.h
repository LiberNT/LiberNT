
#include <cportlib/cportlib.h>

struct _KD_DISPATCH_TABLE;

#define KDP_BOOT_PHASE_BUSES 2
#define KDP_BOOT_PHASE_SYSTEM_ROOT 6

typedef
NTSTATUS
(NTAPI *PKDP_INIT_ROUTINE)(
    _In_ struct _KD_DISPATCH_TABLE *DispatchTable,
    _In_ ULONG BootPhase);

typedef
VOID
(NTAPI *PKDP_PRINT_ROUTINE)(
    _In_ PCCH String,
    _In_ ULONG Length);


VOID

SIZE_T
KdIoReadLine(
    _Out_ PCHAR Buffer,
    _In_ SIZE_T Size);

VOID

NTSTATUS
NTAPI
    _In_ ULONG BootPhase);

VOID



BOOLEAN
NTAPI
KdbpAcquireLock(
    _In_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql);

VOID
NTAPI
KdbpReleaseLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql,
    _In_ BOOLEAN LockAcquired);


VOID

VOID
KdpScreenEnterBugCheck(
    _In_ BOOLEAN FollowPrints);

VOID
KdpScreenHoldOutput(
    _In_ BOOLEAN Hold);

NTSTATUS
NTAPI
KdpScreenInit(
    _In_ struct _KD_DISPATCH_TABLE *DispatchTable,
    _In_ ULONG BootPhase);

VOID

ULONG
NTAPI
KdpInfoQuery(
    _In_ ULONG Class,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Size) PUCHAR Buffer,
    _In_ ULONG Size,
    _Out_ PULONG Total);


