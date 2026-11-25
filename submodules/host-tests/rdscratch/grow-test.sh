#!/bin/sh
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A boot RAM volume grown over its write space stays a consistent NTFS volume that uses the new space
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
set -eu

core=$1
ntfsimg=$2
work=$(mktemp -d "${TMPDIR:-/tmp}/rdscratch.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$ntfsimg" --format "$work/volume.ntfs" 32768 2048 8 LiberNT >/dev/null
head -c 300000 /dev/urandom >"$work/before.bin"
"$ntfsimg" --create-file "$work/volume.ntfs" /before.bin >/dev/null
"$ntfsimg" --write "$work/volume.ntfs" /before.bin 0 "$work/before.bin"
clusters=$("$ntfsimg" --volume-data "$work/volume.ntfs" | awk '$1 == "clusters" { print $2 }')

"$core" "$work/volume.ntfs" "$work/grown.ntfs" 64

"$ntfsimg" --check "$work/grown.ntfs" | grep -q 'errors=0'
grown=$("$ntfsimg" --volume-data "$work/grown.ntfs" | awk '$1 == "clusters" { print $2 }')
test "$grown" -eq $((clusters + 64 * 256)) || {
    echo "FAIL: the grown volume has $grown clusters, expected $((clusters + 64 * 256))"
    exit 1
}
"$ntfsimg" --cat "$work/grown.ntfs" /before.bin | cmp - "$work/before.bin"

head -c 40000000 /dev/urandom >"$work/after.bin"
"$ntfsimg" --create-file "$work/grown.ntfs" /after.bin >/dev/null
"$ntfsimg" --write "$work/grown.ntfs" /after.bin 0 "$work/after.bin"
"$ntfsimg" --cat "$work/grown.ntfs" /after.bin | cmp - "$work/after.bin"
"$ntfsimg" --check "$work/grown.ntfs" | grep -q 'errors=0'
echo "PASS: the grown volume holds a file larger than its loaded part"
