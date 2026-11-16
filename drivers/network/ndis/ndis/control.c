/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/control.c
 * PURPOSE:     Program control routines
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 *              Vizzini (vizzini@plasmic.com)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 *   3 Oct 2003 Vizzini - Formatting and minor bugfixes
 */

#include "ndissys.h"

/*
 * @implemented
 */
VOID
EXPORT
NdisInitializeReadWriteLock(
    IN  PNDIS_RW_LOCK   Lock)
/*
 * FUNCTION: Initialize a NDIS_RW_LOCK
 * ARGUMENTS:
 *     Lock: pointer to the lock to initialize
 * NOTES:
 *    NDIS 5.0
 */
{
  RtlZeroMemory(Lock, sizeof(NDIS_RW_LOCK));

  KeInitializeSpinLock(&Lock->SpinLock);
}

/*
 * @implemented
 */
VOID
EXPORT
NdisAcquireReadWriteLock(
    IN  PNDIS_RW_LOCK   Lock,
    IN  BOOLEAN         fWrite,
    IN  PLOCK_STATE     LockState)
/*
 * FUNCTION:
 * ARGUMENTS:
 * NOTES:
 *    NDIS 5.0
 */
{
  ULONG RefCount, Cpu, Slot;

  ASSERT_IRQL(DISPATCH_LEVEL);

  KeRaiseIrql(DISPATCH_LEVEL, &LockState->OldIrql);
  Cpu = NdisICurrentProcessorIndex();
  if (fWrite || Cpu >= RTL_NUMBER_OF(Lock->RefCount)) {
    if (Lock->Context == PsGetCurrentThread()) {
      KeLowerIrql(LockState->OldIrql);
      LockState->LockState = 2;
      return;
    }

    KeAcquireSpinLockAtDpcLevel(&Lock->SpinLock);
    for (Slot = 0; Slot < RTL_NUMBER_OF(Lock->RefCount); Slot++) {
      if (Slot != Cpu) {
        while (ReadULongAcquire((volatile ULONG *)&Lock->RefCount[Slot].RefCount) != 0)
          YieldProcessor();
      }
    }
    Lock->Context = PsGetCurrentThread();
    LockState->LockState = 4;
  } else {
    RefCount = InterlockedIncrement((PLONG)&Lock->RefCount[Cpu].RefCount);
    /* Racing with a exclusive write lock case. */
    if (ReadULongPtrAcquire(&Lock->SpinLock) != 0) {
      if (RefCount == 1) {
        if (Lock->Context != PsGetCurrentThread()) {
          /* Wait for the exclusive lock to be released. */
          InterlockedDecrement((PLONG)&Lock->RefCount[Cpu].RefCount);
          KeAcquireSpinLockAtDpcLevel(&Lock->SpinLock);
          InterlockedIncrement((PLONG)&Lock->RefCount[Cpu].RefCount);
          KeReleaseSpinLockFromDpcLevel(&Lock->SpinLock);
        }
      }
    }
    LockState->LockState = 3;
  }
}

/*
 * @implemented
 */
VOID
EXPORT
NdisReleaseReadWriteLock(
    IN  PNDIS_RW_LOCK   Lock,
    IN  PLOCK_STATE     LockState)
/*
 * FUNCTION:
 * ARGUMENTS:
 * NOTES:
 *    NDIS 5.0
 */
{
  switch (LockState->LockState) {
    case 2: /* Exclusive write lock, recursive */
      return;

    case 3: /* Shared read lock */
      InterlockedDecrement((PLONG)&Lock->RefCount[NdisICurrentProcessorIndex()].RefCount);
      LockState->LockState = -1;
      if (LockState->OldIrql < DISPATCH_LEVEL)
        KeLowerIrql(LockState->OldIrql);
      return;

    case 4: /* Exclusive write lock */
      Lock->Context = NULL;
      LockState->LockState = -1;
      KeReleaseSpinLock(&Lock->SpinLock, LockState->OldIrql);
      return;
  }
}

VOID
EXPORT
NdisDprAcquireReadWriteLock(
    IN  PNDIS_RW_LOCK   Lock,
    IN  BOOLEAN         fWrite,
    IN  PLOCK_STATE     LockState)
{
  ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
  NdisAcquireReadWriteLock(Lock, fWrite, LockState);
}

VOID
EXPORT
NdisDprReleaseReadWriteLock(
    IN  PNDIS_RW_LOCK   Lock,
    IN  PLOCK_STATE     LockState)
{
  ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
  NdisReleaseReadWriteLock(Lock, LockState);
}

#undef NdisInterlockedPushEntryList
PSINGLE_LIST_ENTRY
EXPORT
NdisInterlockedPushEntryList(
    IN  PSINGLE_LIST_ENTRY  ListHead,
    IN  PSINGLE_LIST_ENTRY  ListEntry,
    IN  PNDIS_SPIN_LOCK     Lock)
{
  return ExInterlockedPushEntryList(ListHead, ListEntry, &Lock->SpinLock);
}

#undef NdisInterlockedPopEntryList
PSINGLE_LIST_ENTRY
EXPORT
NdisInterlockedPopEntryList(
    IN  PSINGLE_LIST_ENTRY  ListHead,
    IN  PNDIS_SPIN_LOCK     Lock)
{
  return ExInterlockedPopEntryList(ListHead, &Lock->SpinLock);
}

/*
 * @implemented
 */
#undef NdisAcquireSpinLock
VOID
EXPORT
NdisAcquireSpinLock(
    IN  PNDIS_SPIN_LOCK SpinLock)
/*
 * FUNCTION: Acquires a spin lock for exclusive access to a resource
 * ARGUMENTS:
 *     SpinLock = Pointer to the initialized NDIS spin lock to be acquired
 */
{
  KeAcquireSpinLock(&SpinLock->SpinLock, &SpinLock->OldIrql);
}

/*
 * @implemented
 */
#undef NdisAllocateSpinLock
VOID
EXPORT
NdisAllocateSpinLock(
    IN  PNDIS_SPIN_LOCK SpinLock)
/*
 * FUNCTION: Initializes for an NDIS spin lock
 * ARGUMENTS:
 *     SpinLock = Pointer to an NDIS spin lock structure
 */
{
  KeInitializeSpinLock(&SpinLock->SpinLock);
}

/*
 * @implemented
 */
#undef NdisDprAcquireSpinLock
VOID
EXPORT
NdisDprAcquireSpinLock(
    IN  PNDIS_SPIN_LOCK SpinLock)
/*
 * FUNCTION: Acquires a spin lock from IRQL DISPATCH_LEVEL
 * ARGUMENTS:
 *     SpinLock = Pointer to the initialized NDIS spin lock to be acquired
 */
{
  KeAcquireSpinLockAtDpcLevel(&SpinLock->SpinLock);
  SpinLock->OldIrql = DISPATCH_LEVEL;
}

/*
 * @implemented
 */
#undef NdisDprReleaseSpinLock
VOID
EXPORT
NdisDprReleaseSpinLock(
    IN  PNDIS_SPIN_LOCK SpinLock)
/*
 * FUNCTION: Releases an acquired spin lock from IRQL DISPATCH_LEVEL
 * ARGUMENTS:
 *     SpinLock = Pointer to the acquired NDIS spin lock to be released
 */
{
  KeReleaseSpinLockFromDpcLevel(&SpinLock->SpinLock);
}

/*
 * @implemented
 */
#undef NdisFreeSpinLock
VOID
EXPORT
NdisFreeSpinLock(
    IN  PNDIS_SPIN_LOCK SpinLock)
/*
 * FUNCTION: Releases a spin lock initialized with NdisAllocateSpinLock
 * ARGUMENTS:
 *     SpinLock = Pointer to an initialized NDIS spin lock
 */
{
  /* Nothing to do here! */
}


/*
 * @implemented
 */
VOID
EXPORT
NdisInitializeEvent(
    IN  PNDIS_EVENT Event)
/*
 * FUNCTION: Initializes an event to be used for synchronization
 * ARGUMENTS:
 *     Event = Pointer to an NDIS event structure to be initialized
 */
{
  KeInitializeEvent(&Event->Event, NotificationEvent, FALSE);
}


/*
 * @implemented
 */
#undef NdisReleaseSpinLock
VOID
EXPORT
NdisReleaseSpinLock(
    IN  PNDIS_SPIN_LOCK SpinLock)
/*
 * FUNCTION: Releases a spin lock previously acquired with NdisAcquireSpinLock
 * ARGUMENTS:
 *     SpinLock = Pointer to the acquired NDIS spin lock to be released
 */
{
  KeReleaseSpinLock(&SpinLock->SpinLock, SpinLock->OldIrql);
}


/*
 * @implemented
 */
VOID
EXPORT
NdisResetEvent(
    IN  PNDIS_EVENT Event)
/*
 * FUNCTION: Clears the signaled state of an event
 * ARGUMENTS:
 *     Event = Pointer to the initialized event object to be reset
 */
{
  KeClearEvent(&Event->Event);
}


/*
 * @implemented
 */
VOID
EXPORT
NdisSetEvent(
    IN  PNDIS_EVENT Event)
/*
 * FUNCTION: Sets an event to a signaled state if not already signaled
 * ARGUMENTS:
 *     Event = Pointer to the initialized event object to be set
 */
{
  KeSetEvent(&Event->Event, IO_NO_INCREMENT, FALSE);
}


/*
 * @implemented
 */
BOOLEAN
EXPORT
NdisWaitEvent(
    IN  PNDIS_EVENT Event,
    IN  UINT        MsToWait)
/*
 * FUNCTION: Waits for an event to become signaled
 * ARGUMENTS:
 *     Event    = Pointer to the initialized event object to wait for
 *     MsToWait = Maximum milliseconds to wait for the event to become signaled
 * RETURNS:
 *     TRUE if the event is in the signaled state
 */
{
  LARGE_INTEGER Timeout;
  PLARGE_INTEGER pTimeout;
  NTSTATUS Status;

  /* Per the NDIS contract (matching Windows), MsToWait == 0 means wait
   * indefinitely, not "poll and return immediately". */
  if (MsToWait == 0)
  {
      pTimeout = NULL;
  }
  else
  {
      Timeout.QuadPart = Int32x32To64(MsToWait, -10000);
      pTimeout = &Timeout;
  }

  Status = KeWaitForSingleObject(&Event->Event, Executive, KernelMode, TRUE, pTimeout);

  return (Status == STATUS_SUCCESS);
}

/* EOF */
