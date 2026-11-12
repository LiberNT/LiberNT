# Wi-Fi stability reference and integration status

## Scope

These fixes were developed for a physical ASUS N550JK with an Intel 7260,
using driver 18.33.17.1. The source fixtures exercise LiberNT code with
controlled kernel/NDIS boundaries. They do not execute Windows NDIS internals
or prove general firmware, IRQL, SMP, Halt or recovery correctness.

The Windows reference ran on the same machine on 2026-11-12: Windows 11 Pro
24H2, build 26100.1742, Audit Mode, with KDNET attached. No Windows
implementation code was read or transcribed. The original installation
media was unchanged; the installed test system received the selected drivers
and tools. The reference was an installed hardware system, not a VM overlay.

## Source fixtures

Windows ran the identical AMD64 binaries used for the fresh LiberNT disk
measurement as SYSTEM (`S-1-5-18`) in interactive session 1 through
Microsoft-signed PsExec 2.43. Every named check and occurrence count matched
native LiberNT and the host run, grouped by case to avoid treating concurrent
print order as a contract. [WIFI-REFERENCE.json](WIFI-REFERENCE.json) retains
those comparisons and executable SHA256 values.

| Fixture | Passing checks | Failed checks |
| --- | ---: | ---: |
| `ndis_reset` | 968 | 0 |
| `ndis_oid` | 667 | 0 |
| `nwifi_rekey` | 47 | 0 |

Each output ended with `RESULT failures=0`. The first process wrapper lost
the OS exit codes; they remain unknown. A separate corrected-wrapper probe
captured exact exit codes 0 and 7 without repeating these reference binaries.
That probe does not retroactively establish their exit codes.

The fixture READMEs link the Microsoft contracts for association completion,
ordinary OID serialization, reset completion, status delivery and passive I/O
work. The event-ordered reset test covers the selected overlapping-generation
schedule. The OID fixture uses controlled reentry, not parallel threads. The
supplicant fixture does not cancel a worker already inside a hardware OID.

## Recorded hardware behavior

On the earlier LiberNT build, a new association installed pairwise, group and
IGTK keys and authorized the secure link. The reset path reached its passive
addressing worker and completed restoration with status zero. After the OID
worker change, disconnect/reconnect completed and the user confirmed working
Internet and responsive windows. These observations establish the measured
cases, not exhaustive driver recovery.

Windows completed five real disconnect/connect cycles with zero command
exit codes and a connected IPv4 state; the user reported no shell or Internet
problem. Workflow times were 1.225, 1.323, 1.317, 1.277 and 6.811 seconds,
including command overhead and one-second polling. They are not isolated
authentication latencies or a Linux performance comparison. All 114 bounded
desktop probes and 114 tray probes received a response during the run.

A later LiberNT STOP 0xF in Intel recovery after debugger resume remains
`INFO_NEEDED`; no vendor-only cause or general recovery fix is claimed.

## Current upstream integration

The draft is based on `ef127fb4ff017222210e9b8c4321d82b3e3e0c0c`.
The earlier component/test builds passed on amd64, i386 and arm64 with
`CCACHE_DISABLE=1` using the existing development build directories. Those
builds and hardware observations preceded this upstream integration. Full
three-architecture builds and a physical boot/reconnect on this exact new
base remain pending. The added firmware-PMK seeding in upstream is retained.

Source review also found an independent blocker in that base: commit
`6b6227517b908df186e1ea3590dcc79ce3d8e404` activates the modern hang-check
timer path, while `MiniCheckForHang` raises to DISPATCH_LEVEL before invoking
the NDIS 6 callback. Microsoft's
[MiniportCheckForHangEx contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nc-ndis-miniport_check_for_hang)
requires PASSIVE_LEVEL. This draft does not change that path. It must be
resolved and measured separately before claiming the integrated Wi-Fi tree
is validated. The current kernel-invariant assertion requirements also need
an integration audit before marking the draft ready.

Cppcheck used the existing amd64 compilation settings and generated headers,
with source/include paths redirected to this integration tree. Findings in
changed files were checked against the source; no suppression was added.
Independent review covered reset ownership, worker generations and OID
serialization. This analysis does not replace the pending new-base runs.
