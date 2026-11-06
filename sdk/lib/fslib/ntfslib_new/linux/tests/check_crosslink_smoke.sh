#!/bin/sh
#
# PROJECT:     LiberNT NTFS library tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Repair an attribute with two cross-linked runs and keep the partner's clusters
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#
set -eu

driver=$1
mkntfs=$2
ntfscp=$3
ntfscat=$4

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-crosslink.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/crosslink.ntfs"

truncate -s 32M "$image"
"$mkntfs" -F -Q -q -c 4096 -L CROSSLINK "$image"
python3 -I -c 'import sys; sys.stdout.buffer.write(bytes((i * 7 + i // 4096) & 0xff for i in range(32768)))' >"$workdir/a.bin"
python3 -I -c 'import sys; sys.stdout.buffer.write(b"b" * 16384)' >"$workdir/b.bin"
"$ntfscp" -f -q "$image" "$workdir/a.bin" /a.bin
"$ntfscp" -f -q "$image" "$workdir/b.bin" /b.bin

# Point the two halves of b.bin at clusters 0-1 and 4-5 of a.bin.
python3 -I - "$image" <<'EOF'
import struct
import sys

def runs(r, pos):
    pairs = pos + struct.unpack_from('<H', r, pos + 0x20)[0]
    out, lcn = [], 0
    while r[pairs]:
        lb, ob = r[pairs] & 15, r[pairs] >> 4
        length = int.from_bytes(r[pairs + 1:pairs + 1 + lb], 'little')
        lcn += int.from_bytes(r[pairs + 1 + lb:pairs + 1 + lb + ob], 'little', signed=True)
        out.append((lcn, length))
        pairs += 1 + lb + ob
    return out

def pair(length, delta):
    width = 1
    while not -(1 << (8 * width - 1)) <= delta < (1 << (8 * width - 1)):
        width += 1
    return bytes([(width << 4) | 1, length]) + delta.to_bytes(width, 'little', signed=True)

image = sys.argv[1]
with open(image, 'r+b') as f:
    data = bytearray(f.read())
    boot = data[:512]
    sector = struct.unpack_from('<H', boot, 0x0B)[0]
    cluster = sector * boot[0x0D]
    raw = struct.unpack_from('<b', boot, 0x40)[0]
    size = 1 << -raw if raw < 0 else raw * cluster
    found = {}
    for off in range(0, len(data) - size, size):
        if data[off:off + 4] != b'FILE':
            continue
        r = bytearray(data[off:off + size])
        usa_ofs, usa_cnt = struct.unpack_from('<HH', r, 4)
        usa = [struct.unpack_from('<H', r, usa_ofs + 2 * i)[0] for i in range(usa_cnt)]
        for i in range(1, usa_cnt):
            struct.pack_into('<H', r, i * sector - 2, usa[i])
        pos = struct.unpack_from('<H', r, 0x14)[0]
        used = struct.unpack_from('<I', r, 0x18)[0]
        name, stream = None, None
        while pos + 8 <= used:
            atype, alen = struct.unpack_from('<II', r, pos)
            if atype == 0xFFFFFFFF or alen == 0:
                break
            if atype == 0x30:
                value = pos + struct.unpack_from('<H', r, pos + 20)[0]
                name = r[value + 66:value + 66 + 2 * r[value + 64]].decode('utf-16le')
            if atype == 0x80 and r[pos + 8] and r[pos + 9] == 0:
                stream = pos
            pos += alen
        if name in ('a.bin', 'b.bin') and stream is not None:
            found[name] = (off, r, usa, stream, used)
    a_runs = runs(found['a.bin'][1], found['a.bin'][3])
    if a_runs[0][1] < 6:
        sys.exit('a.bin first run too short: %s' % a_runs)
    a0 = a_runs[0][0]
    off, r, usa, pos, used = found['b.bin']
    alen = struct.unpack_from('<I', r, pos + 4)[0]
    if pos + alen + 8 != used:
        sys.exit('b.bin $DATA is not the last attribute')
    pairs = pair(2, a0) + pair(2, 4) + b'\0'
    pairs += bytes(-len(pairs) % 8)
    mpo = struct.unpack_from('<H', r, pos + 0x20)[0]
    new_len = mpo + len(pairs)
    if pos + new_len + 8 > size:
        sys.exit('no room in the b.bin record')
    r[pos + mpo:pos + new_len] = pairs
    struct.pack_into('<I', r, pos + 4, new_len)
    r[pos + new_len:pos + new_len + 8] = struct.pack('<II', 0xFFFFFFFF, 0)
    struct.pack_into('<I', r, 0x18, pos + new_len + 8)
    usa_ofs = struct.unpack_from('<H', r, 4)[0]
    for i in range(1, len(usa)):
        struct.pack_into('<H', r, usa_ofs + 2 * i, struct.unpack_from('<H', r, i * sector - 2)[0])
        struct.pack_into('<H', r, i * sector - 2, usa[0])
    data[off:off + size] = r
    f.seek(0)
    f.write(data)
EOF

# Each shared run is reported, not only the first one of the attribute.
if "$driver" --check "$image" >"$workdir/before.txt"; then
    exit 1
fi
test "$(grep -c '^event 4 ' "$workdir/before.txt")" -eq 2

# One repair leaves a consistent volume and a.bin keeps its clusters and data.
"$driver" --check "$image" --fix >"$workdir/fix.txt" || true
"$driver" --check "$image" >"$workdir/after.txt"
"$ntfscat" "$image" /a.bin | cmp - "$workdir/a.bin"
