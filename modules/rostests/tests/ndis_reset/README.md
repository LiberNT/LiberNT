# NDIS 6 reset source regression

`ndis_reset` compiles production `60reset.c` and `workitem.c`, mocking the
kernel, request, miniport and status-delivery boundaries. `thread_host.c`
provides real host threads and events in a separate translation unit so user
and kernel SDK headers do not collide. The adapter's legacy status callbacks
are null, matching the original Intel 7260 crash.

The cases cover synchronous/pending/inline completion, duplicate completion,
DPC addressing completion, allocation/rundown failures, closing admission,
restore-status propagation, reentrant RESET_END and overlapping generations.
Locks model IRQL save/raise/restore and reject recursion. Callbacks must run
outside the lock; blocking restoration must run at PASSIVE_LEVEL. The test
checks both reset-owner rundown pairs, source/header/payload of the modern
status indications, final status, resource release and legacy-callback absence.

The generation test pauses the actual old worker at its tail. A second host
thread completes that reset, starts another actual worker and holds its
callback after inline completion. The old tail must not admit another reset
while the new callback is still active. Events order the schedule; the test
never synthesizes a reset-state transition.

## Rebuild and run

Reuse the existing directories and disable compiler caching:

```sh
CCACHE_DISABLE=1 ninja -C output-Clang-amd64-debug ndis_reset ndis
CCACHE_DISABLE=1 ninja -C output-Clang-i386-debug ndis_reset ndis
CCACHE_DISABLE=1 ninja -C output-Clang-arm64-debug ndis_reset ndis
```

Run the same AMD64 executable on each runtime and retain every named check,
exit code and executable SHA256:

```bat
ndis_reset.exe > ndis-reset-unit.txt 2>&1
echo %ERRORLEVEL%
```

## Evidence and limits

Contracts come from Microsoft's public headers and documentation:

- [NDIS 6 NdisMResetComplete](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nf-ndis-ndismresetcomplete-r1): completion may run at DISPATCH_LEVEL.
- [NdisMIndicateStatusEx](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nf-ndis-ndismindicatestatusex): modern reset-status indications.
- [NdisAllocateIoWorkItem](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nf-ndis-ndisallocateioworkitem) and [NdisQueueIoWorkItem](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nf-ndis-ndisqueueioworkitem): device-associated PASSIVE work, CriticalWorkQueue, callback-owned freeing.
- [ExQueueWorkItem](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-exqueueworkitem): device-associated work must use an I/O work item.

The final source fixture under Wine failed 93 checks against the captured
original source, two on a variant retaining the split worker-tail transition,
and none after the lock fix. `NDIS_RESET_TEST_SOURCE` and
`NDIS_WORKITEM_TEST_SOURCE` select captured source files for such comparisons
without changing test expectations or production files. The committed
[N550JK reference](../WIFI-REFERENCE.md) retains the final Windows comparison.

This is a user-mode source fixture, not a Windows NDIS implementation test.
Rundown, OID completion and real interrupt levels are mocked; event-ordered
threads exercise the selected race, not all SMP interleavings. Real Halt,
filter drainage, firmware and miniport reset still need physical validation.
The current restore path addresses the existing lookahead request; complete
filter/multicast/offload restoration is not proved. Later Windows source-fixture
and reconnect measurements are recorded in
[the N550JK reference](../WIFI-REFERENCE.md).
No Windows 11 parity or hardware-fix claim follows from a source-test pass.

## Recorded hardware validation

The final 22-case fixture passed 968 checks on the development host and the
AMD64 test machine; named per-case results matched. The real Intel adapter
subsequently reached the passive addressing worker and completed restoration
with status zero. Both AMD64 and ARM64 driver/test builds completed. The
source fixture mocks kernel boundaries; the real reset ran in a SYSTEM worker.
This is distinct from the Windows source/API-boundary baseline below.

The identical fresh-install AMD64 fixture subsequently reported 968 passing
checks and no failures on Windows 11 24H2 SYSTEM in session 1. Named per-case
counts matched the native LiberNT run. It still mocks Windows NDIS and IRQL;
the five successful real Windows Wi-Fi reconnects did not force a miniport
reset. Original fixture process exit codes were unavailable due to a wrapper
bug; complete check output and a separate corrected exit-code probe are kept
in the reference report.

A later STOP 0xF in the Intel driver's recovery path after debugger resume
remains INFO_NEEDED. It does not establish a cause or prove general Wi-Fi
stability; the validated scope here is reset completion and lifetime.

The recorded hardware runs preceded the upstream integration. See the reference
report for the new-base validation status and retained limitations.
