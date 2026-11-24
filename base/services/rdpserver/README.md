# LiberNT RDP console server

`rdpserver.exe` is the Remote Desktop server of LiberNT. Once Remote Desktop
is turned on, the user signed in at the console gets the console session,
one client at a time; a client that signs in replaces the one already
connected. Any other user gets a session of their own, started for them on a
remote display adapter and kept for their next connection. FreeRDP implements
the wire protocol, TLS and CredSSP; the server captures the desktop and
injects keyboard and mouse input with `SendInput` once a client has
authenticated. The RDP drive redirection channel also
carries authenticated file transfers to a configured local directory.

## Building

The server is part of every build. It links statically against FreeRDP 3.32.0
(`sdk/lib/3rdparty/freerdp`: WinPR, the core library and the server with the
`drdynvc` and `rdpdr` channels) and OpenSSL 3.5.4 (`sdk/lib/3rdparty/openssl`,
without assembler and with the legacy provider built in for NTLM), so the
images carry a single `%SystemRoot%\system32\rdpserver\rdpserver.exe`, its
`rdpserver.ini`, the automatic `LiberNTRDPServer` service and the FreeRDP and
OpenSSL licenses in `licenses`.

The service (`rdpserver.exe /service [ini]`) runs in session 0, which has no
interactive desktop. While `fDenyTSConnections` under
`HKLM\SYSTEM\CurrentControlSet\Control\Terminal Server` is 0, it listens on
port 3389 and runs the whole protocol itself: TLS, Network Level
Authentication, the codecs and the channels. It closes the port and drops its
connections within seconds when the value changes back to 1. A connection
reaches a session only after NLA has signed its user in, and the service picks
the session from that user's account, never from anything the client sends
before:

- the user signed in at the console, or any user while nobody is signed in
  there, gets the console session;
- a user with a session the service started earlier gets that session back;
- any other user gets a new session: the service creates a
  `RdpVdd_RemoteDisplay&NewSession_<n>` adapter and starts the session through
  the session manager.

In the chosen session the service starts an agent (`/agent`) as SYSTEM. The
agent shows the session on a remote display adapter, signs the user in through
Winlogon with the NLA credentials, copies each changed region of the display
to a section it shares with the service, reads the pointer and injects the
client's keyboard and mouse input with `SendInput`. The service and the agent
talk over the pipe `\\.\pipe\LiberNT-RDPServer-<session>`, which only SYSTEM
can open; each side checks that the other end is the process it expects. A new
connection of the same user replaces the one already connected; the agent
leaves the display when the last connection of its session ends. A session the
service started ends when its user signs out or when the service stops. If the
service ends abnormally, its sessions keep running, disconnected, as on Windows
11; the service lists them in the volatile key
`HKLM\SYSTEM\CurrentControlSet\Services\LiberNTRDPServer\Sessions`, and once
started again it gives each one back to its user on a new
`RdpVdd_RemoteDisplay&SessionId_<n>` adapter.
*Settings > System > Remote Desktop* sets the value, and it is 1 on a new
installation, as on Windows 11. The agent follows the desktop that receives
input, so a client sees and drives the logon screen and the lock screen as well
as the user's desktop. Started by hand, the server needs a controller
configuration (see below).

Port 3389 listens on every address and only accepts Network
Level Authentication (CredSSP): a client that offers only TLS or standard
RDP security gets `HYBRID_REQUIRED_BY_SERVER`. The credentials go through the
Negotiate package, and the server logs the user on with `LogonUser`. The
account needs a password, and it, or one of its enabled groups, must hold
`SeRemoteInteractiveLogonRight` without `SeDenyRemoteInteractiveLogonRight`;
a new installation grants the right to Administrators and Remote Desktop
Users. At its first start the server creates a self-signed certificate for
the computer name (RSA 2048, server authentication, valid 183 days) as
`server.crt` and `server.key` next to `rdpserver.exe`; the key is readable by
SYSTEM and Administrators only. It creates a new one when the certificate has
expired or either file is missing.

## Configuration

`rdpserver.ini` only holds the optional file transfer root. The in-tree file
leaves it out; to set it, keep a private directory outside the source tree
with an `rdpserver.ini` and point the build at it with
`-DRDPSERVER_CONFIG_DIR=/path/to/private/rdpserver`:

```ini
[server]
transfer_root=C:\
```

`transfer_root` enables file transfers below that local directory after
authentication. Leave it out to disable transfers.

Authentication, the first frame and disconnects are logged as `RDPTEST`
lines on the kernel debugger and on stdout. An idle server captures nothing;
a connected session captures 30 frames per second, or the rate
`DWMFRAMEINTERVAL` under `...\Terminal Server\WinStations` sets (four frames
per second each, up to 60), and sends the changed 64x32 tiles in batched
bitmap updates.

## Client and file transfer

Connect with any Remote Desktop client, such as mstsc or a FreeRDP 3 client
with `/sec:nla`, and a local account allowed to log on remotely. For file
transfers, also redirect a dedicated host directory as the `ROSXFER` drive
(`/drive:ROSXFER,<directory>`).

The server polls `request.txt` in that share. A request is UTF-8 text with
LF line ends: a header `RDPFILES1<TAB><job id of 32 hex digits>`, then one
line per item, `<operation><TAB><size><TAB><sha256><TAB><source><TAB><destination>`:

- `PUT`: copy the share file `<source>` to `<destination>` below
  `transfer_root`, checking `<size>` and the SHA-256. A destination that
  already has that size and hash is skipped. The copy goes to a temporary
  name that is verified, flushed and renamed over the destination, so an
  interrupted upload keeps the previous file.
- `GET`: copy `<source>` below `transfer_root` to the share file
  `<destination>`; the size field must be a number and the hash field is
  not checked.
- `MKDIR`: create the directory `<destination>`.

The server writes `results/<job id>.txt` to the share: `RDPRESULT1<TAB><job>`,
one `<index><TAB><status><TAB><bytes><TAB><sha256><TAB>COPY|SKIP` line per
item, and `END<TAB><job>`. A job runs once; after a reconnect the same job
restarts and skips the files already verified.

Paths must be relative, without traversal, alternate data streams, device
names or reparse points, and fit in `MAX_PATH` with room for the temporary
suffix. One job holds at most 16,384 items and a 4 MiB request. Transfers do
not keep ACLs or timestamps.

FreeRDP 3.32.0's client `channels/drive/client/drive_file.c` passes the wrong
handle to `GetFileSize`: reads run past the end of the file and the server
rejects the request. Use a client with that call fixed.

## Test controller

A private `rdpserver.ini` with `enabled=1` replaces the Remote Desktop server
with a console server for one test controller on an explicitly configured
LAN, for example an automated client that drives real hardware. Keep it with
its TLS certificate and key in a directory outside the source tree and point
the build at it with `-DRDPSERVER_CONFIG_DIR`; its `rdpserver.ini`, `*.crt`,
`*.key` and `*.pem` files replace the in-tree configuration:

```ini
[server]
enabled=1
bind=10.42.0.2
allow=10.42.0.1
username=ros-test
password=REPLACE_WITH_A_RANDOM_TEST_PASSWORD
certificate=server.crt
privatekey=server.key
interface_mac=02:de:ad:10:ec:68,20:46:a1:08:d0:53
netmask=255.255.255.0
transfer_root=C:\
```

- The service starts it whatever `fDenyTSConnections` says. It listens on
  port 3389 of `bind` only, accepts one client at a time and only from the
  `allow` address, over TLS with these credentials; NLA is not enabled. Use a
  random password of at least 16 characters and pin the certificate
  fingerprint in the client.
- It shows the console as it is: no remote display, no lock of the physical
  console, and the display control channel is not used.
- A relative `certificate` or `privatekey` path names a file next to the
  configuration, so the same configuration works on the live image (`X:`)
  and on an installed disk.
- `interface_mac` (a comma-separated list of up to eight addresses) and
  `netmask` are optional. With them, the server waits for a network adapter
  with one of these MAC addresses, adds `bind` to it with `AddIPAddress`,
  keeping its other addresses, and removes that address again on a clean
  exit. Without them, an adapter must already have the `bind` address.
- An invalid configuration makes the server exit with code 2, and the
  service does not start it again until the service restarts.
