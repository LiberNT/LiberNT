#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>
#
# Writes the second-stage MSI of an LLVMpipe release payload from its zip
# (sdk/tools/mesa_llvmpipe_payload.py): the system32 files, the licenses under
# Windows\3rdParty\mesa-llvmpipe and the OpenGL and Vulkan driver registration.
# The MSI is written by wixl and msibuild (msitools).

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
import uuid
import zipfile
from pathlib import Path
from xml.sax.saxutils import quoteattr

MSI_ARCH = {
    "amd64": ("x64", "System64Folder", "yes", "x64;1033"),
    "arm64": ("x64", "System64Folder", "yes", "Arm64;1033"),
    "riscv64": ("x64", "System64Folder", "yes", "x64;1033"),
}
UPGRADE_NAMESPACE = uuid.UUID("9a4c2f7e-51d3-4b8a-8e16-7c0d3f5b2a91")
MSOGL = r"SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\MSOGL"
DIRECTORIES = []


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def directory_xml(root, path, prefix, win64, components, indent):
    lines = []
    for entry in sorted(path.iterdir()):
        if entry.is_dir():
            DIRECTORIES.append(entry)
            ident = f"{prefix}d{len(DIRECTORIES)}"
            lines.append(f'{indent}<Directory Id="{ident}" Name={quoteattr(entry.name)}>')
            lines += directory_xml(root, entry, prefix, win64, components, indent + "  ")
            lines.append(f"{indent}</Directory>")
        else:
            ident = f"{prefix}{len(components)}"
            components.append(ident)
            lines.append(f'{indent}<Component Id="{ident}" Guid="*" Win64="{win64}">'
                         f'<File Id="f{ident}" Source={quoteattr(str(entry))} KeyPath="yes"/></Component>')
    return lines


def write_msi(stage, arch, release, version, output, work):
    wixl_arch, folder, win64, template = MSI_ARCH[arch]
    upgrade = str(uuid.uuid5(UPGRADE_NAMESPACE, arch)).upper()
    system_components = []
    system_lines = directory_xml(stage, stage / "system32", "s", win64, system_components, "        ")
    license_components = []
    license_lines = directory_xml(stage, stage / "licenses", "l", win64, license_components, "            ")
    refs = "\n".join(f'      <ComponentRef Id="{c}"/>' for c in system_components + license_components)
    system_lines = "\n".join(system_lines)
    license_lines = "\n".join(license_lines)
    wxs = work / "llvmpipe.wxs"
    wxs.write_text(f'''<?xml version="1.0" encoding="utf-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="*" Name="LiberNT LLVMpipe {release}" Language="1033" Version="{version}"
           Manufacturer="LiberNT" UpgradeCode="{upgrade}">
    <Package InstallerVersion="500" Compressed="yes" InstallScope="perMachine"/>
    <MajorUpgrade DowngradeErrorMessage="A newer LiberNT LLVMpipe is already installed."/>
    <Media Id="1" Cabinet="llvmpipe.cab" EmbedCab="yes"/>
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="{folder}">
{system_lines}
        <Component Id="registration" Guid="*" Win64="{win64}">
          <RegistryValue Root="HKLM" Key={quoteattr(MSOGL)} Name="DLL" Type="string" Value="mesa_llvmpipe.dll" KeyPath="yes"/>
          <RegistryValue Root="HKLM" Key={quoteattr(MSOGL)} Name="Version" Type="integer" Value="2"/>
          <RegistryValue Root="HKLM" Key={quoteattr(MSOGL)} Name="DriverVersion" Type="integer" Value="1"/>
          <RegistryValue Root="HKLM" Key={quoteattr(MSOGL)} Name="Flags" Type="integer" Value="1"/>
          <RegistryValue Root="HKLM" Key="SOFTWARE\\Khronos\\Vulkan\\Drivers" Name="[{folder}]lvp_icd.json" Type="integer" Value="0"/>
          <RegistryValue Root="HKLM" Key="SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"
                         Name="VK_ADD_DRIVER_FILES" Type="expandable" Value="%SystemRoot%\\System32\\lvp_icd.json"/>
        </Component>
      </Directory>
      <Directory Id="WindowsFolder">
        <Directory Id="ThirdParty" Name="3rdParty">
          <Directory Id="MesaLlvmpipe" Name="mesa-llvmpipe">
{license_lines}
          </Directory>
        </Directory>
      </Directory>
    </Directory>
    <Feature Id="LLVMpipe" Level="1">
{refs}
      <ComponentRef Id="registration"/>
    </Feature>
  </Product>
</Wix>
''')
    subprocess.run(["wixl", "-a", wixl_arch, "-o", str(output), str(wxs)], check=True)
    if arch == "arm64":
        subprocess.run(["msibuild", str(output), "-s", f"LiberNT LLVMpipe {release}", "LiberNT",
                        template, "{" + str(uuid.uuid4()).upper() + "}"], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zip", required=True, type=Path, help="libernt-mesa-llvmpipe-<release>-<arch>.zip")
    parser.add_argument("--arch", required=True, choices=sorted(MSI_ARCH))
    parser.add_argument("--release", required=True, help="payload release, e.g. 26.2.2-1")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    for tool in ("wixl", "msibuild"):
        if not shutil.which(tool):
            sys.exit(f"{tool} (msitools) is required")
    numbers = [int(part) for part in args.release.split("-", 1)[0].split(".")[:3]]
    numbers += [0] * (3 - len(numbers))
    version = ".".join(str(n) for n in numbers + [int(args.release.rsplit("-", 1)[1])])
    args.out.mkdir(parents=True, exist_ok=True)
    output = args.out / f"libernt-mesa-llvmpipe-{args.release}-{args.arch}.msi"
    with tempfile.TemporaryDirectory() as work:
        stage = Path(work) / "stage"
        with zipfile.ZipFile(args.zip) as archive:
            archive.extractall(stage)
        for required in ("system32/mesa_llvmpipe.dll", "system32/vulkan_lvp.dll", "system32/lvp_icd.json"):
            if not (stage / required).is_file():
                sys.exit(f"{args.zip} lacks {required}")
        write_msi(stage, args.arch, args.release, version, output, Path(work))
    print(f"{sha256(output)}  {output.name}  {output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
