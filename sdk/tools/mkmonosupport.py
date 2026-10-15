#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

import argparse
import os
import shutil
import subprocess
import sys
import tempfile


def read_table(directory, name):
    path = os.path.join(directory, name + ".idt")
    if not os.path.exists(path):
        return []
    with open(path, encoding="cp1252") as handle:
        lines = handle.read().splitlines()
    header = lines[0].split("\t")
    rows = []
    for line in lines[3:]:
        if not line:
            continue
        values = line.split("\t")
        values += [""] * (len(header) - len(values))
        rows.append(dict(zip(header, values)))
    return rows


def directory_path(directories, key):
    parts = []
    while key and key != "TARGETDIR":
        entry = directories[key]
        default = entry["DefaultDir"].split("|")[-1].split(":")[-1]
        if key == "WindowsFolder":
            break
        if default != ".":
            parts.append(default)
        key = entry["Directory_Parent"]
    if key != "WindowsFolder":
        raise SystemExit("unsupported directory root for " + key)
    return "\\".join(reversed(parts))


def inf_quote(text):
    return '"' + text.replace('"', '""') + '"'


def registry_line(root, key, name, value, component_dir):
    if root != "2":
        raise SystemExit("unsupported registry root " + root)
    if name in ("*", "+"):
        if value:
            raise SystemExit("key marker with a value under " + key)
        return 'HKLM,%s,,0x00000010' % inf_quote(key)
    if name == "-":
        raise SystemExit("unsupported registry delete marker under " + key)
    value = value.replace("[$" + component_dir[0] + "]", component_dir[1])
    if "[" in value:
        raise SystemExit("unsupported formatted value %s under %s" % (value, key))
    if value.startswith("##"):
        return 'HKLM,%s,%s,0x00000000,%s' % (inf_quote(key), inf_quote(name), inf_quote(value[1:]))
    if value.startswith("#%"):
        return 'HKLM,%s,%s,0x00020000,%s' % (inf_quote(key), inf_quote(name), inf_quote(value[2:]))
    if value.startswith("#x"):
        raise SystemExit("unsupported binary value under " + key)
    if value.startswith("#"):
        return 'HKLM,%s,%s,0x00010001,%d' % (inf_quote(key), inf_quote(name), int(value[1:]))
    return 'HKLM,%s,%s,0x00000000,%s' % (inf_quote(key), inf_quote(name), inf_quote(value))


def main():
    parser = argparse.ArgumentParser(description="Preinstall the Wine Mono support package contents")
    parser.add_argument("msi")
    parser.add_argument("arch")
    parser.add_argument("system_root")
    parser.add_argument("out_dir")
    args = parser.parse_args()

    is64 = args.arch != "i386"
    for tool in ("msidump", "msiextract"):
        if not shutil.which(tool):
            raise SystemExit(tool + " (msitools) is required to preinstall the Wine Mono support package")

    work = tempfile.mkdtemp(prefix="monosupport-")
    try:
        subprocess.run(["msidump", "-d", work, args.msi], check=True, stdout=subprocess.DEVNULL)
        files_dir = os.path.join(args.out_dir, "files")
        shutil.rmtree(files_dir, ignore_errors=True)
        os.makedirs(files_dir)
        subprocess.run(["msiextract", "-C", files_dir, args.msi], check=True, stdout=subprocess.DEVNULL)

        directories = {row["Directory"]: row for row in read_table(work, "Directory")}
        components = {row["Component"]: row for row in read_table(work, "Component")}

        def installed(component):
            condition = components[component]["Condition"]
            if condition in ("", None):
                return True
            if condition == "(VersionNT64)":
                return is64
            raise SystemExit("unsupported component condition " + condition)

        lines = ["[Version]", 'Signature = "$Windows NT$"', "", "[AddReg]"]
        for row in read_table(work, "Registry"):
            component = row["Component_"]
            if not installed(component):
                continue
            attributes = int(components[component]["Attributes"] or "0")
            key = row["Key"]
            if is64 and not attributes & 0x100:
                key = key.replace("Software\\", "Software\\WOW6432Node\\", 1)
            target = "%s\\%s\\" % (args.system_root, directory_path(directories, components[component]["Directory_"]))
            lines.append(registry_line(row["Root"], key, row["Name"], row["Value"], (component, target)))
        with open(os.path.join(args.out_dir, "monosupport.inf"), "w", encoding="ascii", newline="\r\n") as handle:
            handle.write("\n".join(lines) + "\n")

        entries = []
        for row in read_table(work, "File"):
            component = row["Component_"]
            if not installed(component):
                continue
            relative = directory_path(directories, components[component]["Directory_"])
            name = row["FileName"].split("|")[-1]
            source = None
            for root, _, names in os.walk(files_dir):
                for candidate in names:
                    full = os.path.join(root, candidate)
                    rel = os.path.relpath(full, files_dir).replace(os.sep, "\\")
                    if rel.lower().endswith((relative + "\\" + name).lower()):
                        source = full
            if not source:
                raise SystemExit("extracted file not found: %s\\%s" % (relative, name))
            entries.append("%s/%s=%s" % (relative.replace("\\", "/"), name, source))
        with open(os.path.join(args.out_dir, "monosupport.lst"), "w", encoding="utf-8") as handle:
            handle.write("\n".join(entries) + "\n")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
