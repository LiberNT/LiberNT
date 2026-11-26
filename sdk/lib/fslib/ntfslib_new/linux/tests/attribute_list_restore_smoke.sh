#!/bin/sh
#
# PROJECT:     LiberNT NTFS library tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Repair keeps an extension record listed when its attribute list entry went stale
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#
set -eu

driver=$1
mkntfs=$2
ntfscp=$3

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-attrlist.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/attrlist.ntfs"
cluster="$workdir/cluster.bin"
frame="$workdir/frame.bin"
expected="$workdir/expected.bin"
target=/fragmented.bin

truncate -s 16M "$image"
"$mkntfs" -F -Q -q -c 4096 "$image" 2>/dev/null
dd if=/dev/zero bs=4096 count=1 status=none | tr '\000' C >"$cluster"

files=0
while [ $files -lt 4000 ] &&
      "$ntfscp" -f -q "$image" "$cluster" "/c$files.bin" 2>/dev/null; do
    files=$((files + 1))
done
i=0
while [ $i -lt $files ]; do
    "$driver" --truncate "$image" "/c$i.bin" 0 >/dev/null
    i=$((i + 2))
done

"$driver" --create-file "$image" "$target" >/dev/null
: >"$expected"
offset=0
i=0
while [ $i -lt $((files / 2 - 64)) ]; do
    dd if=/dev/urandom bs=4120 count=1 status=none >"$frame"
    "$driver" --write "$image" "$target" $offset "$frame" >/dev/null
    cat "$frame" >>"$expected"
    offset=$((offset + 4120))
    i=$((i + 1))
done
"$driver" --cat "$image" "$target" | cmp - "$expected"
"$driver" --check "$image" | grep -q ' errors=0$'

# A torn update: the list entry of one $DATA extension keeps an older sequence number and starting VCN.
python3 -I - "$image" "${target#/}" <<'EOF'
import struct
import sys

image, name = sys.argv[1], sys.argv[2]
with open(image, 'r+b') as f:
    data = bytearray(f.read())
    boot = data[:512]
    sector = struct.unpack_from('<H', boot, 0x0B)[0]
    cluster = sector * boot[0x0D]
    raw = struct.unpack_from('<b', boot, 0x40)[0]
    size = 1 << -raw if raw < 0 else raw * cluster
    for off in range(0, len(data) - size, size):
        if data[off:off + 4] != b'FILE':
            continue
        r = bytearray(data[off:off + size])
        usa_ofs, usa_cnt = struct.unpack_from('<HH', r, 4)
        for i in range(1, usa_cnt):
            r[i * sector - 2:i * sector] = r[usa_ofs + 2 * i:usa_ofs + 2 * i + 2]
        pos = struct.unpack_from('<H', r, 0x14)[0]
        used = struct.unpack_from('<I', r, 0x18)[0]
        names, listpos = [], None
        while pos + 8 <= used:
            atype, alen = struct.unpack_from('<II', r, pos)
            if atype == 0xFFFFFFFF or alen == 0:
                break
            if atype == 0x30:
                value = pos + struct.unpack_from('<H', r, pos + 20)[0]
                names.append(r[value + 66:value + 66 + 2 * r[value + 64]].decode('utf-16le'))
            if atype == 0x20:
                listpos = pos
            pos += alen
        if name not in names:
            continue
        assert listpos is not None, 'the fragmented stream has no attribute list'
        number = struct.unpack_from('<I', r, 0x2C)[0]
        if r[listpos + 8]:
            length = struct.unpack_from('<Q', r, listpos + 0x30)[0]
            pairs = listpos + struct.unpack_from('<H', r, listpos + 0x20)[0]
            extents, lcn = [], 0
            while r[pairs]:
                lb, ob = r[pairs] & 15, r[pairs] >> 4
                count = int.from_bytes(r[pairs + 1:pairs + 1 + lb], 'little')
                lcn += int.from_bytes(r[pairs + 1 + lb:pairs + 1 + lb + ob], 'little', signed=True)
                extents.append((lcn, count))
                pairs += 1 + lb + ob
            stored = bytearray(b''.join(bytes(data[l * cluster:(l + c) * cluster]) for l, c in extents))
        else:
            value = listpos + struct.unpack_from('<H', r, listpos + 0x14)[0]
            length = struct.unpack_from('<I', r, listpos + 0x10)[0]
            stored = r[value:value + length]
        entry = 0
        while entry < length:
            etype, elen = struct.unpack_from('<IH', stored, entry)
            ref = struct.unpack_from('<Q', stored, entry + 0x10)[0]
            if etype == 0x80 and ref & 0xFFFFFFFFFFFF != number:
                struct.pack_into('<Q', stored, entry + 0x10, (ref & 0xFFFFFFFFFFFF) | ((((ref >> 48) + 0x100) & 0xFFFF) << 48))
                struct.pack_into('<Q', stored, entry + 8, struct.unpack_from('<Q', stored, entry + 8)[0] + 1)
                break
            entry += elen
        else:
            raise SystemExit('no $DATA extension entry')
        if r[listpos + 8]:
            done = 0
            for l, c in extents:
                f.seek(l * cluster)
                f.write(stored[done:done + c * cluster])
                done += c * cluster
            break
        r[value:value + length] = stored
        usn = struct.unpack_from('<H', r, usa_ofs)[0]
        for i in range(1, usa_cnt):
            r[usa_ofs + 2 * i:usa_ofs + 2 * i + 2] = r[i * sector - 2:i * sector]
            struct.pack_into('<H', r, i * sector - 2, usn)
        f.seek(off)
        f.write(r)
        break
    else:
        raise SystemExit('target record not found')
EOF

check=$("$driver" --check "$image" || true)
echo "$check" | grep -q '^event 21 '
"$driver" --check "$image" --fix >/dev/null || true
"$driver" --check "$image" | grep -q ' errors=0$'
"$driver" --cat "$image" "$target" | cmp - "$expected"
