# AMD64 KE state-machine inventory

This is a working inventory, not a claim that every transition or interleaving
has been found. Source paths below are relative to the repository root. Existing
tests are leads, not evidence of Windows parity unless their results are compared
with Windows for the same binary and configuration.

| State or protocol | Implementation entry points | Existing test leads | Coverage in `NtKeDispatcherTransitions` |
| --- | --- | --- | --- |
| Notification and synchronization events: reset, set, pulse, wait consumption | `ntoskrnl/ke/eventobj.c`, `ntoskrnl/ke/wait.c` | `ntos_ke/KeEvent.c` | Initial/reset, repeated set, consumption and reset; pulse and concurrent waiters remain |
| Semaphore count, limit and wait consumption | `ntoskrnl/ke/semphobj.c`, `ntoskrnl/ke/wait.c` | `ntos_ke/KeSemaphore.c` | Release, overflow without count change, drain and refill; concurrent wakeups remain |
| Mutant ownership, recursion, abandonment and wakeup | `ntoskrnl/ke/mutex.c`, `ntoskrnl/ke/wait.c`, `ntoskrnl/ke/thrdobj.c` | `ntos_ke/KeMutex.c`, `apitests/ntdll/NtMutant.c` | Unowned release and recursive acquisition/release; abandonment and cross-thread contention remain |
| Wait registration, wait-any/all, timeout, alert and satisfaction | `ntoskrnl/ke/wait.c` | `ntos_ke/KeWaitMultiple.c` | Immediate wait-any/all selection and consumption, failed wait-all, timeout and user APC; blocked-wait races and alert paths remain |
| Timer insertion, cancellation, expiration, rearming and periodic DPC | `ntoskrnl/ke/timerobj.c`, `ntoskrnl/ke/dpc.c`, `ntoskrnl/ke/clock.c` | `ntos_ke/KeTimer.c`, `ntos_ke/KeTimer2KM.c` | One-shot notification/synchronization timers, cancel before expiry, expire and rearm; periodic timers, DPC and races remain |
| APC insertion, pending, delivery, removal and rundown; disable and process attachment | `ntoskrnl/ke/apc.c`, `ntoskrnl/ke/procobj.c`, `ntoskrnl/ke/thrdobj.c` | `ntos_ke/KeApc.c`, `KeApcInject.c`, `KeApcScheduling.c`, `KeCriticalRegionKM.c` | Self-queued user APC survives a nonalertable wait and executes once in an alertable wait; kernel APC, rundown and attachment remain |
| DPC insertion, duplicate rejection, targeting, execution, removal, flush and threaded delivery | `ntoskrnl/ke/dpc.c` | `ntos_ke/KeDpc.c`, `KeDpcRemove.c`, `KeDpcFlush.c`, `KeThreadedDpc.c` | Not covered |
| Kernel queue entries, active thread limit, waiter activation and rundown | `ntoskrnl/ke/queue.c` | `ntos_ke/KeQueue.c` | Not covered |
| Device queue idle/busy, ordered insertion and removal | `ntoskrnl/ke/devqueue.c` | `ntos_ke/KeDevQueue.c` | Not covered |
| Gate and guarded mutex acquisition, contention and release | `ntoskrnl/ke/gate.c`, `ntoskrnl/ke/gmutex.c`, `ntoskrnl/ke/wait.c` | `ntos_ke/KeGuardedMutex.c` | Not covered |
| Thread initialization, ready/standby/running/waiting, deferred ready, context switch, termination and reaping | `ntoskrnl/ke/thrdobj.c`, `ntoskrnl/ke/thrdschd.c`, `ntoskrnl/ke/amd64/ctxswitch.S` | `ntos_ke/KeDeferredReady.c`, `ntos_ke/amd64/KeSwitchHandoff.c` | Only indirectly exercised; transition ordering and races remain |
| Thread suspension, resumption, freeze, alert, priority and affinity | `ntoskrnl/ke/thrdobj.c`, `ntoskrnl/ke/thrdschd.c` | `ntos_ke/KeAffinityKM.c`, `KeQueryBasePriority.c` | Not covered |
| Process initialization, attachment, affinity and scheduling parameters | `ntoskrnl/ke/procobj.c`, `ntoskrnl/ke/processor.c`, `ntoskrnl/ke/procgrp.c` | `ntos_ke/KeProcessStackCount.c`, `KeProcessor.c` | Not covered |
| Clock publication, adjustment, suspend and resume | `ntoskrnl/ke/clock.c`, `ntoskrnl/ke/time.c` | `ntos_ke/KeTime.c` | Not covered |
| Interrupt connection, dispatch and disconnection; IRQL transitions | `ntoskrnl/ke/amd64/interrupt.c`, `ntoskrnl/ke/amd64/irql.c`, `ntoskrnl/ke/amd64/trap.S` | `ntos_ke/KeIrql.c` | Not covered |
| Cross-processor requests and freeze/thaw coordination | `ntoskrnl/ke/amd64/ipi.c`, `ntoskrnl/ke/amd64/freeze.c`, `ntoskrnl/ke/freeze.c` | `ntos_ke/KeIpiKM.c` | Not covered |
| Processor extended-state save and restore | `ntoskrnl/ke/amd64/xstate.c`, `ntoskrnl/ke/amd64/ctxswitch.S` | `ntos_ke/KeFloatPointState.c` | Not covered |
| Profiling start, interrupt sampling and stop | `ntoskrnl/ke/profobj.c` | Search API and kernel tests before testing | Not covered |

The new test is user-mode and exercises only observable API contracts. It does
not inspect private Windows structures or establish correctness of internal
locking, scheduling, memory ordering, or architecture-specific transitions.
The AMD64 Windows baseline is host build 29683. The same binary passed in a
four-vCPU LiberNT AMD64 KVM snapshot. Both runs completed 85 checks with no
failures or skips. These observations do not cover other builds, architectures,
privilege contexts, or concurrent interleavings.

## Scheduler, APC and DPC follow-up

`apitests/ntdll/NtKeHandoff.c` performs 512 signal-and-wait handoffs on one
CPU and 512 across two CPUs. The same AMD64 binary passed all 20 checks on
the Windows host (build 29683) and in the four-vCPU LiberNT snapshot. This
observes progress and acknowledgements, not internal ready-queue transitions,
and does not prove that the early self-wake window was reached.
The existing `apitests/kernel32/SuspendThread.c` test also passed the same
131,099 checks on both systems, covering nested suspension and repeated
suspend/resume handoffs. This is observable API behavior, not proof of a
particular kernel APC implementation.

`ntos_ke/KeApcDpcTransitions.c` tests special kernel APC insertion, duplicate
rejection, removal, reinsertion and delivery (9 checks), and normal DPC
insertion, duplicate rejection, argument preservation, removal, reinsertion and
delivery (17 checks). Both tests passed in the LiberNT AMD64 snapshot. They
have not been run on Windows: the full kernel test driver and private APC ABI
have not been validated against the host's Windows build, so a LiberNT pass
is not evidence that loading it on the host is safe.

Microsoft documents that a queued DPC cannot be queued a second time and that
a failed duplicate insertion performs no operation:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-keinsertqueuedpc
The host uses Windows Server build 29683; results are a host observation, not
a Windows 11 release baseline. Kernel APC rundown, normal APC suppression,
threaded DPCs, DPC targeting, and scheduler race interleavings remain open.
