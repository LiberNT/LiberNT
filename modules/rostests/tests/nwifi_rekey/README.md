# NWIFI association key-state regression

`nwifi_rekey` compiles the production NWIFI supplicant wrapper and links the
production RSNA state machine and cryptography. NDIS locking, work scheduling,
key OIDs and frame delivery are mocked; the fixture constructs authenticated
EAPOL messages and drains the actual key-install workers.

The checks cover initial pairwise/group installation, a new successful
association, retransmitted messages, group-only updates, a queued worker from
an earlier association and rejection of that association's old message 3 with
the new nonce. A new association must install its pairwise key and authorize
the link; a group update must preserve the existing pairwise key. The fixture
does not establish general replay protection or model concurrent workers.

## Rebuild and repeat

Use the existing build directories, with compiler caching disabled:

```sh
CCACHE_DISABLE=1 ninja -C output-Clang-amd64-debug nwifi nwifi_rekey
CCACHE_DISABLE=1 ninja -C output-Clang-i386-debug nwifi nwifi_rekey
CCACHE_DISABLE=1 ninja -C output-Clang-arm64-debug nwifi nwifi_rekey
```

Run the AMD64 executable on the target and keep each named check and exit code:

```bat
nwifi_rekey.exe > nwifi-rekey-unit.txt 2>&1
echo %ERRORLEVEL%
```

## Evidence and limits

Microsoft's [association completion contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/windot11/ns-windot11-dot11_association_completion_parameters)
defines `bPortAuthorized`. LiberNT's caller starts host key authorization when
this is false; it stops the host supplicant for the authorized/offloaded path.
The physical Intel 7260 log showed the second host-authorized association
running only a group-key worker, followed by the handshake timeout. Source
inspection found that the old association's `KeysInstalled` and RSNA state
survived `NwifiSupplicantSetAssociation`.

The final fixture produced ten failed checks against the old source and zero
against the fix in separate Wine 11.0 runs on macOS. Wine is a source regression runtime, not a Windows 11 baseline. The later physical Windows comparison is recorded in
[the N550JK reference](../WIFI-REFERENCE.md). ARM64 was build-checked only.

On the AMD64 LiberNT machine the same fixture passed all 47 checks. After
loading the updated driver, the Intel 7260 completed a real roaming event:
association, pairwise/group/IGTK installation and secure link authorization.
The user also confirmed working reconnection, desktop interaction and internet
access. These observations validate this association-reset case, not Windows
11 parity or the distinct pending CONNECT wait chain.

The test does not prove real IRQL, SMP, firmware or miniport behavior. A worker
already inside a hardware OID is not modeled. The pending CONNECT OID and the
service/Explorer wait chain were addressed by the separate OID change; this
association regression does not prove their cause or recovery. Real association/roaming and responsive
UI must be checked after loading the driver.

The identical fresh-install fixture reported 47 passing checks and no failures
under Windows 11 24H2 SYSTEM in session 1, matching native LiberNT per-case
counts. Five real Intel 7260 reconnects completed and the user reported working
Internet and shell interaction. The fixtures still use LiberNT's supplicant;
neither their output nor a reconnect identifies Windows internal key-worker
behavior. The original process exit codes were lost by the wrapper, while the
full check output remains available in the reference report.

The recorded hardware runs preceded the upstream integration. See the reference
report for the new-base validation status and retained limitations.
