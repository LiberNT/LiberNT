#!/bin/sh
#
# PROJECT:     LiberNT NTFS library tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Format a 4096-byte sector volume and round-trip files through it
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#
set -eu

driver=$1
ntfsfix=${2:-}

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-4k.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/4k.ntfs"

head -c 300000 /dev/urandom >"$workdir/big.bin"
printf 'small\n' >"$workdir/small.txt"
printf 'Windows/small.txt=%s\nWindows/sub/big.bin=%s\n' \
    "$workdir/small.txt" "$workdir/big.bin" >"$workdir/files.lst"

"$driver" --format "$image" 16384 2048 1 FOURK 4096
test "$(wc -c <"$image" | tr -d ' ')" -eq $((16384 * 4096))
test "$(od -An -tu2 -j11 -N2 "$image" | tr -d ' ')" -eq 4096
test "$(od -An -tu4 -j28 -N4 "$image" | tr -d ' ')" -eq 2048

probe=$("$driver" --probe "$image")
printf '%s\n' "$probe" | grep -qx 'sector size: 4096'
printf '%s\n' "$probe" | grep -qx 'cluster size: 4096'
printf '%s\n' "$probe" | grep -qx 'label: FOURK'

"$driver" --addfiles "$image" "$workdir/files.lst"
"$driver" --cat "$image" Windows/small.txt | cmp - "$workdir/small.txt"
"$driver" --cat "$image" Windows/sub/big.bin | cmp - "$workdir/big.bin"
"$driver" --check "$image" | grep -q ' errors=0$'

if "$driver" --format "$workdir/bad.ntfs" 16384 0 1 BAD 8192 2>/dev/null; then
    exit 1
fi
if "$driver" --format "$workdir/bad.ntfs" 16384 0 1 BAD 1536 2>/dev/null; then
    exit 1
fi

"$driver" --format "$workdir/512.ntfs" 131072 0 8 DEFAULT
test "$(od -An -tu2 -j11 -N2 "$workdir/512.ntfs" | tr -d ' ')" -eq 512

if [ -n "$ntfsfix" ]; then
    "$ntfsfix" -n "$image" >/dev/null
    "$ntfsfix" -n "$workdir/512.ntfs" >/dev/null
fi
