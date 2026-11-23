#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>
#
# Builds the ICU payload of a LiberNT release in a Docker container:
#   libernt-icu-<release>-<arch>.zip  system32\icu.dll and the license, cataloged by the build
#   libernt-icu-<release>-<arch>.msi  what the second stage installs over the stub icu.dll
# icu.dll is ICU4C with the data built in, the unversioned C API exported and a 72.1 file
# version, like the icu.dll of Windows 11. The MSI is written by wixl and msibuild (msitools).

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import uuid
import zipfile
from pathlib import Path
from xml.sax.saxutils import quoteattr

ICU_VERSION = "72.1"
ICU_SOURCE = "https://github.com/unicode-org/icu/releases/download/release-72-1/icu4c-72_1-src.tgz"
ICU_SHA256 = "a2d2d38217092a7ed56635e34467f92f976b370e20182ad325edea6681a71d68"
LLVM_MINGW = "llvm-mingw-20260616-ucrt-ubuntu-22.04-aarch64"
LLVM_MINGW_URL = f"https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/{LLVM_MINGW}.tar.xz"
LLVM_MINGW_SHA256 = "e7e5d135d93d3f2a3beaaea633a5b0e66ac75391a53feae654391913dd76102b"
LICENSE = "ICU-LICENSE"
TRIPLE = {"amd64": "x86_64-w64-mingw32", "arm64": "aarch64-w64-mingw32", "i386": "i686-w64-mingw32"}
SYMBOL_PREFIX = {"amd64": "", "arm64": "", "i386": "_"}
DATA_LIB = {"amd64": "sicudt.a", "arm64": "sicudt.a", "i386": "libsicudt.a"}
MSI_ARCH = {
    "amd64": ("x64", "System64Folder", "yes", "x64;1033"),
    "arm64": ("x64", "System64Folder", "yes", "Arm64;1033"),
    "i386": ("x86", "SystemFolder", "no", "Intel;1033"),
}
LICENSE_NAME = {"amd64": LICENSE, "arm64": LICENSE, "i386": f"{LICENSE}-x86"}
UPGRADE_NAMESPACE = uuid.UUID("3d1f6a52-8b0e-4c47-a6f2-9e57c1b4d208")

BUILD = r'''
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential python3 curl ca-certificates xz-utils >/dev/null
cd /build
curl -sSL -o icu.tgz "$ICU_SOURCE"
echo "$ICU_SHA256  icu.tgz" | sha256sum -c -
curl -sSL -o llvm-mingw.tar.xz "$LLVM_MINGW_URL"
echo "$LLVM_MINGW_SHA256  llvm-mingw.tar.xz" | sha256sum -c -
tar -xzf icu.tgz
tar -xJf llvm-mingw.tar.xz
export PATH="/build/$LLVM_MINGW/bin:$PATH"
mkdir host cross
(cd host && ../icu/source/runConfigureICU Linux --disable-tests --disable-samples >/dev/null && make -j"$(nproc)" >/dev/null)
(cd cross && ../icu/source/configure --host="$TRIPLE" --with-cross-build=/build/host \
    --enable-static --disable-shared --with-data-packaging=static --disable-renaming \
    --disable-tests --disable-samples --disable-extras --disable-tools \
    CC="$TRIPLE-clang" CXX="$TRIPLE-clang++" >/dev/null && make -j"$(nproc)" >/dev/null)
LIBS="cross/lib/libsicuin.a cross/lib/libsicuuc.a cross/lib/$DATA_LIB"
{
    echo "LIBRARY icu.dll"
    echo "EXPORTS"
    llvm-nm --defined-only --extern-only -P cross/lib/libsicuuc.a cross/lib/libsicuin.a 2>/dev/null |
        awk '$2 == "T" { print $1 }' | sed "s/^$SYMBOL_PREFIX//" | grep -v '^_Z' |
        grep -E '^[A-Za-z][A-Za-z0-9_]*$' | grep -v '^uprv_' | sort -u
} > icu.def
cat > icu.rc <<RC
1 VERSIONINFO
FILEVERSION 72,1,0,2
PRODUCTVERSION 72,1,0,2
FILEOS 0x40004
FILETYPE 0x2
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "CompanyName", "Unicode, Inc."
      VALUE "FileDescription", "ICU for LiberNT"
      VALUE "FileVersion", "72.1.0.2"
      VALUE "InternalName", "icu"
      VALUE "OriginalFilename", "icu.dll"
      VALUE "ProductName", "International Components for Unicode"
      VALUE "ProductVersion", "72.1.0.2"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
RC
"$TRIPLE-windres" icu.rc -O coff -o icu.res
"$TRIPLE-clang++" -shared -o /out/icu.dll icu.def icu.res \
    -Wl,--whole-archive $LIBS -Wl,--no-whole-archive -static -s -ladvapi32
[ "$(stat -c %s /out/icu.dll)" -gt 20000000 ] || { echo "icu.dll lacks the ICU data" >&2; exit 1; }
cp icu/LICENSE /out/ICU-LICENSE
'''


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build(arch, out):
    env = {
        "ICU_SOURCE": ICU_SOURCE, "ICU_SHA256": ICU_SHA256,
        "LLVM_MINGW": LLVM_MINGW, "LLVM_MINGW_URL": LLVM_MINGW_URL, "LLVM_MINGW_SHA256": LLVM_MINGW_SHA256,
        "TRIPLE": TRIPLE[arch], "SYMBOL_PREFIX": SYMBOL_PREFIX[arch],
        "DATA_LIB": DATA_LIB[arch],
    }
    command = ["docker", "run", "--rm", "--platform", "linux/arm64", "-v", f"{out}:/out"]
    for key, value in env.items():
        command += ["-e", f"{key}={value}"]
    command += ["debian:bookworm", "bash", "-c", "mkdir -p /build && " + BUILD]
    subprocess.run(command, check=True)


def write_zip(root, arch, release, output):
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(root / "icu.dll", Path("system32") / "icu.dll")
        archive.write(root / LICENSE, Path("licenses") / LICENSE)
        archive.writestr("payload.json", json.dumps({
            "name": "libernt-icu",
            "release": release,
            "arch": arch,
            "icu": ICU_VERSION,
            "license": "Unicode-3.0",
            "system32": ["icu.dll"],
        }, indent=2) + "\n")


def write_msi(root, arch, release, version, output, work):
    wixl_arch, folder, win64, template = MSI_ARCH[arch]
    upgrade = str(uuid.uuid5(UPGRADE_NAMESPACE, arch)).upper()
    wxs = work / "icu.wxs"
    wxs.write_text(f'''<?xml version="1.0" encoding="utf-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="*" Name="LiberNT ICU {release}" Language="1033" Version="{version}"
           Manufacturer="LiberNT" UpgradeCode="{upgrade}">
    <Package InstallerVersion="500" Compressed="yes" InstallScope="perMachine"/>
    <MajorUpgrade DowngradeErrorMessage="A newer LiberNT ICU is already installed."/>
    <Media Id="1" Cabinet="icu.cab" EmbedCab="yes"/>
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="{folder}">
        <Component Id="icu" Guid="*" Win64="{win64}"><File Id="icu" Source={quoteattr(str(root / "icu.dll"))} KeyPath="yes"/></Component>
      </Directory>
      <Directory Id="WindowsFolder">
        <Directory Id="ThirdParty" Name="3rdParty">
          <Component Id="license" Guid="*" Win64="{win64}"><File Id="license" Name="{LICENSE_NAME[arch]}" Source={quoteattr(str(root / LICENSE))} KeyPath="yes"/></Component>
        </Directory>
      </Directory>
    </Directory>
    <Feature Id="ICU" Level="1">
      <ComponentRef Id="icu"/>
      <ComponentRef Id="license"/>
    </Feature>
  </Product>
</Wix>
''')
    subprocess.run(["wixl", "-a", wixl_arch, "-o", str(output), str(wxs)], check=True)
    if arch == "arm64":
        subprocess.run(["msibuild", str(output), "-s", f"LiberNT ICU {release}", "LiberNT",
                        template, "{" + str(uuid.uuid4()).upper() + "}"], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", required=True, choices=sorted(MSI_ARCH))
    parser.add_argument("--release", required=True, help="payload release, e.g. 72.1-1")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--no-build", action="store_true", help="package an existing icu.dll in --out")
    args = parser.parse_args()

    for tool in ("wixl", "msibuild") + (() if args.no_build else ("docker",)):
        if not shutil.which(tool):
            sys.exit(f"{tool} is required")
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    if not args.no_build:
        build(args.arch, args.out)
    if not (args.out / "icu.dll").is_file() or not (args.out / LICENSE).is_file():
        sys.exit(f"{args.out} lacks icu.dll or {LICENSE}")

    version = ".".join(ICU_VERSION.split(".") + ["0", args.release.rsplit("-", 1)[1]])
    stem = f"libernt-icu-{args.release}-{args.arch}"
    with tempfile.TemporaryDirectory() as work:
        write_zip(args.out, args.arch, args.release, args.out / f"{stem}.zip")
        write_msi(args.out, args.arch, args.release, version, args.out / f"{stem}.msi", Path(work))
    for suffix in (".zip", ".msi"):
        path = args.out / f"{stem}{suffix}"
        print(f"{sha256(path)}  {path.name}  {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
