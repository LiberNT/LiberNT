#!/bin/sh
#
# PROJECT:     LiberNT NTFS library tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Decide startup repair from the $Volume flags Windows 11 writes
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#
set -eu

driver=$1
mkntfs=$2

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-startup.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/startup.ntfs"

truncate -s 32M "$image"
"$mkntfs" -F -Q -q -L STARTUP "$image"

set_flags()
{
    python3 -I - "$image" "$1" <<'EOF'
import struct
import sys

image, flags = sys.argv[1], int(sys.argv[2], 16)
with open(image, 'r+b') as f:
    data = bytearray(f.read())
    sector = struct.unpack_from('<H', data, 0x0B)[0]
    cluster = sector * data[0x0D]
    raw = struct.unpack_from('<b', data, 0x40)[0]
    size = 1 << -raw if raw < 0 else raw * cluster
    for lcn_offset in (0x30, 0x38):
        off = struct.unpack_from('<Q', data, lcn_offset)[0] * cluster + 3 * size
        r = bytearray(data[off:off + size])
        usa_ofs, usa_cnt = struct.unpack_from('<HH', r, 4)
        usa = [struct.unpack_from('<H', r, usa_ofs + 2 * i)[0] for i in range(usa_cnt)]
        for i in range(1, usa_cnt):
            struct.pack_into('<H', r, i * sector - 2, usa[i])
        pos = struct.unpack_from('<H', r, 0x14)[0]
        while struct.unpack_from('<I', r, pos)[0] != 0x70:
            pos += struct.unpack_from('<I', r, pos + 4)[0]
        value = pos + struct.unpack_from('<H', r, pos + 20)[0]
        struct.pack_into('<H', r, value + 10, flags)
        for i in range(1, usa_cnt):
            struct.pack_into('<H', r, usa_ofs + 2 * i, struct.unpack_from('<H', r, i * sector - 2)[0])
            struct.pack_into('<H', r, i * sector - 2, usa[0])
        data[off:off + size] = r
    f.seek(0)
    f.write(data)
EOF
}

# Values read from Windows 11 volumes: 0x0101 after FSCTL_MARK_VOLUME_DIRTY
# (full chkdsk at startup), 0x0201 after its own corruption detection
# (online scan only, no startup repair).
for case in 0000:no 0001:yes 0101:yes 0201:no 0301:yes 0200:no; do
    flags=${case%%:*}
    expected=${case#*:}
    set_flags "$flags"
    "$driver" --probe "$image" >"$workdir/probe.txt"
    grep -q "^volume flags: 0x$flags\$" "$workdir/probe.txt"
    grep -q "^startup repair: $expected\$" "$workdir/probe.txt"
done
