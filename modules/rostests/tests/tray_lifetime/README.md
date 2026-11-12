# Tray image and power icon lifetime

This source fixture compiles the production `syspager.cpp` and `power.cpp`.
It uses native hidden toolbar/tooltip windows and unchanged Fluent battery
resources. Kernel/OS parity is not inferred from this fixture. Window callback,
allocation, image extraction and queue-allocation boundaries allow controlled
failure and reentry. Only the fixture changes render size to force a rebuild;
production tray icons stay at 18 px.

The cases cover caller icon destruction before image-list resize, shared-slot
modification/deletion, deletion of an entry without an image, transactional
resize failure, queued custom icon snapshots, repeated queued-source removal,
nested close/delete/re-add, tooltip-position reentry and allocation failures.
The power cases run 5000 updates, retry a failed ADD, reject a failed icon load,
and exercise nested initialization/shutdown and no-battery hiding.

Contracts come from Microsoft documentation:

- [LoadImageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadimagew) and [DestroyIcon](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroyicon): caller ownership when LR_SHARED is absent.
- [Taskbar notification icons](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar): the caller can destroy its icon after Shell_NotifyIcon returns.
- [NOTIFYICONDATAW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataw): shared icons, NIIF_USER, hBalloonIcon and its null fallback.
- [TTM_SETTITLE](https://learn.microsoft.com/en-us/windows/win32/controls/ttm-settitle): the tooltip copies a supplied custom icon.
- [ImageList_GetIcon](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-imagelist_geticon): extraction returns a caller-owned icon.

Reuse existing build directories and disable compiler caching:

```sh
CCACHE_DISABLE=1 ninja -C output-Clang-amd64-debug tray_lifetime explorer stobject
CCACHE_DISABLE=1 ninja -C output-Clang-i386-debug tray_lifetime explorer stobject
CCACHE_DISABLE=1 ninja -C output-Clang-arm64-debug tray_lifetime explorer stobject
```

Run the same AMD64 executable and retain every named check, exit code and hash:

```bat
tray_lifetime.exe > tray-lifetime-unit.txt 2>&1
echo %ERRORLEVEL%
```

An ADD whose balloon preparation fails is rolled back before returning FALSE,
so process-watcher registration and toolbar-row lifetime stay consistent.
For a failed balloon update, an existing registered row remains valid and
failure is reported; atomic rollback of all combined MODIFY fields is not
claimed as Windows behavior.

The [N550JK reference](../TRAY-REFERENCE.md) records the Windows source/API
boundary comparison, executable hash, named checks and hardware observations.
An installed Explorer boot and sustained resource measurements remain
necessary before declaring an integrated leak fixed.

## Recorded hardware validation

The final 17-case fixture passed 69 named checks on the host and AMD64 machine,
with identical ordered results. Its power loop created and destroyed 5001
icons, with peak ownership one and no remaining owned icon. After installing
the pair and rebooting, the user confirmed normal windows and tray icons.
Both AMD64 and ARM64 production/test builds completed. Total integrated
Explorer handle counts over a long runtime were not measured.

On Windows 11 24H2, the identical fresh-install fixture reported 69 passing
checks and no failures under SYSTEM in session 1. Named per-case check counts
matched native LiberNT. The separate real Windows tray responded to all 114
bounded probes during five Wi-Fi reconnects. The approximately 115-second
USER/GDI sample includes UI activity and does not establish integrated leak
absence. Original fixture process exit codes were not captured; the output
and the corrected wrapper's separate exit-code validation are retained in the
reference report.

The recorded hardware runs preceded the upstream integration. See the reference
report for the new-base validation status and retained limitations.
