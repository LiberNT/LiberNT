#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 Ahmed ARIF
"""Generate a content-based OS image catalog for the kernel's read-only data."""

import hashlib
import pathlib
import struct
import sys


def catalog_identity(path, data):
    if len(data) < 264 or data[:2] != b"MZ":
        raise ValueError(f"System image is not a PE file: {path}")
    nt = struct.unpack_from("<I", data, 60)[0]
    if nt > len(data) - 264 or data[nt:nt + 4] != b"PE\0\0":
        raise ValueError(f"System image has no PE header: {path}")
    magic = struct.unpack_from("<H", data, nt + 24)[0]
    if magic not in (0x10b, 0x20b):
        raise ValueError(f"System image has an unknown optional header: {path}")
    directory = 24 + (112 if magic == 0x20b else 96)
    if (struct.unpack_from("<H", data, nt + 20)[0] < directory - 24 + 40 or
            struct.unpack_from("<I", data, nt + directory - 4)[0] < 5):
        raise ValueError(f"System image has no certificate directory: {path}")
    checksum = nt + 24 + 64
    entry = nt + directory + 32
    offset, size = struct.unpack_from("<II", data, entry)
    end = len(data)
    if size and offset <= len(data) and size == len(data) - offset:
        end = offset
    if end < entry + 8:
        raise ValueError(f"System image is truncated: {path}")
    while end > entry + 8 and data[end - 1] == 0:
        end -= 1
    digest = hashlib.sha256(data[:checksum] + data[checksum + 4:entry] + data[entry + 8:end]).digest()
    return end, digest


def main():
    manifest, output = map(pathlib.Path, sys.argv[1:3])
    names = manifest.read_text().splitlines()
    if len(sys.argv) > 3:
        names += [name for name in pathlib.Path(sys.argv[3]).read_text().splitlines()
                  if name and pathlib.Path(name).exists()]
    entries = set()
    for name in names:
        path = pathlib.Path(name)
        entries.add(catalog_identity(path, path.read_bytes()))
    if not entries:
        raise ValueError("The system image catalog must not be empty")
    lines = ["/* Generated from this build's system images. Do not edit. */",
             "static const struct { ULONGLONG Size; UCHAR Hash[32]; } MiSystemImageCatalog[] = {"]
    for size, digest in sorted(entries):
        values = ", ".join(f"0x{byte:02x}" for byte in digest)
        lines.append(f"    {{{size}ULL, {{{values}}}}},")
    lines.append("};\n")
    content = "\n".join(lines)
    if not output.exists() or output.read_text() != content:
        temporary = output.with_suffix(".tmp")
        temporary.write_text(content)
        temporary.replace(output)


if __name__ == "__main__":
    main()
