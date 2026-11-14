# Tray lifetime reference and integration status

## Scope

The fixture compiles LiberNT `syspager.cpp` and `power.cpp` with controlled
allocation, queue and callback boundaries. Native USER/GDI calls run on
Windows, but the shell implementation under test remains LiberNT's. This is
not a test of Windows Explorer internals or proof of integrated leak absence.
The production tray icon size and Fluent artwork are unchanged.

## Windows and LiberNT source comparison

The reference ran on the physical ASUS N550JK on 2026-11-12: Windows 11 Pro
24H2, build 26100.1742, Audit Mode, with KDNET attached. The original media was
unchanged; the installed test system received the selected drivers and tools.
It was an installed hardware reference, not a VM overlay. No Windows
implementation code was read or transcribed.

The same AMD64 executable used for the fresh LiberNT disk measurement ran as
SYSTEM (`S-1-5-18`) in interactive session 1 through Microsoft-signed PsExec
2.43. All 69 named checks passed, with zero failed checks. Every check and
occurrence count matched native LiberNT and the host run.
[TRAY-REFERENCE.json](TRAY-REFERENCE.json) retains the executable SHA256 and
per-case comparisons. The power loop created and destroyed 5001 icons, with
peak ownership one and no remaining owned icon.

The first wrapper lost the OS process exit code. It remains unknown;
`RESULT failures=0` is retained as program output only. A separate corrected
wrapper probe captured exact exit codes 0 and 7 without repeating the fixture.

The fixture README links the Microsoft ownership/copy contracts for icons,
notification data and tooltips. The cases exercise image rebuild failure,
shared slots, queue snapshots, deletion/re-addition and nested callbacks.

## Hardware observations and limits

After the earlier LiberNT Explorer/stobject pair was installed and the machine
rebooted, the user confirmed normal windows, power/network icons and desktop
interaction. This physical check predates the upstream integration below.

During five Windows Wi-Fi reconnects, all 114 bounded desktop probes and 114
tray probes received responses. Maximum times were 33.324 ms and 5.051 ms.
The approximately 115-second interactive sample included menus, a folder and
a browser. Explorer GDI counts ranged from 150 to 248 and ended at 181; USER
counts ranged from 210 to 404 and ended at 282. Handles rose from 2610 to
3498, with peak 4277. These values establish neither sustained leak absence
nor that the remaining growth is excess. Matched long-duration measurements
remain necessary.

## Current upstream integration

The draft is based on `ef127fb4ff017222210e9b8c4321d82b3e3e0c0c`.
Earlier component/test builds passed on amd64, i386 and arm64 with
`CCACHE_DISABLE=1` in the existing development build directories. Those runs
preceded this upstream integration; full three-architecture builds and a
physical boot of this exact new-base shell pair remain pending.

Cppcheck used the existing amd64 compilation settings and generated headers,
with source/include paths redirected to this integration tree. Findings in
the changed files were reviewed without adding suppressions. Independent
review checked snapshot ownership, callback reentry, shared image retirement
and power-icon release. These checks do not replace the pending integrated
boot or establish long-runtime Explorer resource parity.
