#!/usr/bin/env python3
"""Boot LiberNT in QEMU and run the guest perf/stress suite.

Builds a FAT payload disk (launcher.cmd + test binaries), boots the image
with an e1000e user-net NIC and the applaunch SMBIOS marker, then drives the
suite through the in-guest telnet shell and prints a result table.

    run_perftest.py --img LiberNT.img --build <outdir> --work <dir> \
        [--qemu qemu-system-x86_64] [--skip-wifi] [--results out.txt]

Host requirements: python3, qemu-system-x86_64, OVMF. Everything else comes
from the build tree (native-fatten, native-mkdiskimg) and the guest itself.
"""
import argparse
import os
import re
import shlex
import socket
import subprocess
import sys
import tempfile
import time

HOST_GW = "10.0.2.2"
TELNET_PORT = 2323

LAUNCHER = """@echo off
net start telnetd > telnet.txt 2>&1
"""

# (name, guest command, wait seconds, result parser)
NET_SUITE = [
    ("ping",       "ping {gw} -n 4",                      30, None),
    ("conn_rate",  "wsperf c {gw} 5002 100",              60, r"conn/s"),
    ("req_resp",   "wsperf r {gw} 5001 50",              120, r"req/s"),
    ("tcp_tx",     "wsperf t {gw} 5002 32",               90, r"MB/s"),
    ("tcp_duplex", "wsperf t {gw} 5001 32 e",            120, None),
    ("tcp_rx",     "wsperf d {gw} 5003",                  90, r"MB/s"),
    ("udp_echo",   "wsperf u {gw} 5003 500",             120, r"dgram/s"),
    ("refused",    "wsperf f {gw} 5999 50",               60, r"/s"),
    ("err_paths",  "wsperf x {gw} 5999",                  30, None),
    ("select_rt",  "wsperf s {gw} 5001 50",              120, r"/s"),
    ("peek",       "wsperf p {gw} 5001 50",               90, None),
    ("parallel",   "wsperf m {gw} 5002 8 8",             120, r"MB/s"),
    ("rst_storm",  "wsperf b {gw} 5001 64",              120, r"/s"),
]

DISK_SUITE = [
    ("disk_wr_rd",  "benchdisk D:\\benchd.tmp 48",        120, r"MB/s"),
    ("disk_unbuf",  "benchdisk D:\\benchu.tmp 48 -u",     120, r"MB/s"),
]

WIFI_SUITE = [
    ("vwifi_install", "pnputil /add-driver C:\\Windows\\inf\\netvwifi.inf /install", 90, None),
    ("wifi_scan",     "wlantest scan",                   30, r"bssid"),
    ("wifi_connect",  "wlantest ReactOS-Open",           45, r"state="),
    ("wifi_flood",    "wlantest flood 32",               90, r"MB/s"),
    ("wifi_disc",     "wlantest disconnect",             20, None),
]


class Guest:
    """cmd.exe inside the guest over the forwarded telnet port."""

    def __init__(self, port=TELNET_PORT, user=b"tester", password=b"x"):
        self.port = port
        self.user = user
        self.password = password
        self.sock = None

    @staticmethod
    def _clean(raw):
        text = raw.decode("latin1", "replace")
        text = re.sub(r"\xff[\xfb\xfc\xfd\xfe].", "", text)
        text = re.sub(r"\xff\xfa..\x01\xff\xf0", "", text)
        return re.sub(r"\xff\xf0", "", text)

    def _read(self, timeout):
        out = b""
        end = time.time() + timeout
        while time.time() < end:
            try:
                chunk = self.sock.recv(65536)
                if not chunk:
                    raise ConnectionError("guest closed telnet")
                out += chunk
                # Stop once the command prompt reappears; the command finished
                if re.search(rb"(?m)^[A-Za-z]:[\\/][^>\r\n]*>\s*$", out):
                    break
            except socket.timeout:
                pass
        return self._clean(out)

    def connect(self, timeout=180):
        end = time.time() + timeout
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), 5)
                break
            except OSError:
                if time.time() > end:
                    raise
                time.sleep(2)
        self.sock.settimeout(0.4)
        self._read(6)            # handshake + banner
        self.sock.sendall(self.user + b"\r")
        self._read(1.5)
        self.sock.sendall(self.password + b"\r")
        self._read(3)

    def cmd(self, command, wait=5.0):
        self.sock.sendall(command.encode() + b"\r")
        return self._read(wait)

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None


def build_payload(work, build, tools, launcher=LAUNCHER):
    """Render payload dir, then fatten + mkdiskimg it with the native tools."""
    fatten = os.path.join(build, "host-tools", "bin", "fatten")
    mkdiskimg = os.path.join(build, "host-tools", "bin", "mkdiskimg")
    for cand in (fatten, mkdiskimg):
        if not os.path.isfile(cand):
            # older layout: tools land in the build root bin
            alt = os.path.join(build, "bin", os.path.basename(cand))
            if os.path.isfile(alt):
                continue
            sys.exit(f"native tool not found: {cand}")

    payload = os.path.join(work, "payload")
    os.makedirs(payload, exist_ok=True)
    with open(os.path.join(payload, "launcher.cmd"), "w", newline="\r\n") as f:
        f.write(launcher)
    for tool in tools:
        src = os.path.join(build, tool)
        if not os.path.isfile(src):
            sys.exit(f"guest tool not built: {src}")
        subprocess.run(["cp", src, payload], check=True)

    fat = os.path.join(work, "payload.fat")
    img = os.path.join(work, "payload.img")
    sectors = 131072  # 64 MiB; smaller sizes fall under the FAT32 minimum and fatten fails
    subprocess.run([
        fatten, fat, "-format", str(sectors), "fat32", "APPDISK",
        *[arg for name in os.listdir(payload) for arg in ("-add", os.path.join(payload, name), name)],
    ], check=True)
    subprocess.run([
        mkdiskimg, "-o", img, "-partition", fat, "-start", "2048", "-type", "0x0C",
    ], check=True)
    return img


def spawn_qemu(img, payload_img, work, qemu, ovmf_code, ovmf_vars, serial):
    log = os.path.join(work, "serial.log")
    argv = [
        qemu,
        "-machine", "q35,accel=kvm:tcg",
        "-cpu", "max",
        "-smp", "2",
        "-m", "2048",
        "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
        "-drive", f"format=raw,file={img},if=none,id=libernt0",
        "-device", "ich9-ahci,id=ahci",
        "-device", "ide-hd,drive=libernt0,bus=ahci.0",
        "-device", "qemu-xhci",
        "-drive", f"if=none,id=app0,format=raw,file={payload_img}",
        "-device", "ide-hd,drive=app0,bus=ahci.1",
        "-display", "none",
        "-netdev", f"user,id=n0,hostfwd=tcp:127.0.0.1:{TELNET_PORT}-:23",
        "-device", "e1000e,netdev=n0",
        "-smbios", "type=11,value=libernt.applaunch",
        "-serial", f"file:{log}",
        "-monitor", "none",
        "-pidfile", os.path.join(work, "qemu.pid"),
    ]
    proc = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    return proc, log


def run_suite(guest, suite, results):
    for name, command, wait, key in suite:
        output = guest.cmd(command.format(gw=HOST_GW), wait)
        results[name] = output
        first = next((ln.strip() for ln in output.splitlines() if ln.strip()
                      and not ln.strip().startswith(command[:40])), "")
        flag = ""
        if key:
            flag = " [OK]" if re.search(key, output) else " [MISS]"
        print(f"  {name:<16} {first[:72]}{flag}", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--img", required=True, help="LiberNT.img from the build")
    ap.add_argument("--build", required=True, help="cmake build directory")
    ap.add_argument("--work", default=None, help="scratch dir (temp if omitted)")
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    ap.add_argument("--ovmf-code", default=None)
    ap.add_argument("--ovmf-vars", default=None)
    ap.add_argument("--skip-wifi", action="store_true")
    ap.add_argument("--results", default=None, help="write raw outputs here")
    ap.add_argument("--boot-timeout", type=int, default=300)
    args = ap.parse_args()

    work = args.work or tempfile.mkdtemp(prefix="perftest-")
    os.makedirs(work, exist_ok=True)
    img = os.path.abspath(args.img)
    if not os.path.isfile(img):
        sys.exit(f"image not found: {img}")

    if not args.ovmf_code or not args.ovmf_vars:
        for cand in ("/usr/share/OVMF/OVMF_CODE_4M.fd",
                     "/usr/share/OVMF/OVMF_CODE_4M.ms.fd",
                     "/usr/share/edk2/ovmf/OVMF_CODE.fd",
                     "/usr/share/qemu/OVMF.fd"):
            if os.path.isfile(cand):
                args.ovmf_code = args.ovmf_code or cand
                if "CODE" in os.path.basename(cand):
                    args.ovmf_vars = args.ovmf_vars or cand.replace("CODE", "VARS")
                break
        if not args.ovmf_code or not os.path.isfile(args.ovmf_code):
            sys.exit("OVMF code firmware not found; pass --ovmf-code")
        if not args.ovmf_vars or not os.path.isfile(args.ovmf_vars):
            sys.exit("OVMF vars template not found; pass --ovmf-vars")

    guest_tools = [
        "modules/rostests/tests/bench/wsperf.exe",
        "modules/rostests/tests/bench/benchdisk.exe",
        "base/applications/network/wlantest/wlantest.exe",
    ]
    payload_img = build_payload(work, os.path.abspath(args.build), guest_tools)

    responders = subprocess.Popen(
        [sys.executable, os.path.join(os.path.dirname(__file__), "responders.py")],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)

    ovmf_vars = os.path.join(work, "OVMF_VARS.fd")
    subprocess.run(["cp", args.ovmf_vars, ovmf_vars], check=True)
    qemu, log = spawn_qemu(img, payload_img, work, args.qemu,
                           args.ovmf_code, ovmf_vars, "serial.log")
    print(f"QEMU pid {qemu.pid}, serial log {log}")

    results = {}
    rc = 0
    guest = Guest()
    try:
        guest.connect(timeout=args.boot_timeout)
        print("telnet up, running net suite", flush=True)
        run_suite(guest, NET_SUITE, results)
        print("disk suite", flush=True)
        run_suite(guest, DISK_SUITE, results)
        if not args.skip_wifi:
            print("wifi suite", flush=True)
            run_suite(guest, WIFI_SUITE, results)
        results["netstat"] = guest.cmd("netstat -s", 15)
    except (OSError, ConnectionError) as e:
        print(f"FAIL: {e}")
        rc = 1
    finally:
        guest.close()
        qemu.terminate()
        try:
            qemu.wait(10)
        except subprocess.TimeoutExpired:
            qemu.kill()
        responders.terminate()

    if args.results:
        with open(args.results, "w") as f:
            for name, out in results.items():
                f.write(f"===== {name} =====\n{out}\n")

    missing = [n for n, _, _, k in NET_SUITE + DISK_SUITE + WIFI_SUITE
               if k and n in results and not re.search(k, results[n])]
    if missing:
        print("steps with no measured result:", ", ".join(missing))
        rc = rc or 1
    sys.exit(rc)


if __name__ == "__main__":
    main()
