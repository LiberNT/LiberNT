#!/bin/sh
#
# PROJECT:     LiberNT NTFS library tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Keep $MFT growth contiguous in the MFT zone while file data fills the volume
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#
set -eu

frontend=$1

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-mft-zone.XXXXXX")
trap 'rm -r -- "$workdir"' EXIT HUP INT TERM
image="$workdir/test.ntfs"
data="$workdir/data.bin"
list="$workdir/list.txt"
count=25000

truncate -s 256M "$image"
"$frontend" --format "$image" 524288 0 1 ZONE >/dev/null
head -c 1500 /dev/zero | tr '\0' 'z' >"$data"

# Without an MFT zone, $MFT outgrows record 0 after about 16900 of these files.
awk -v n="$count" -v src="$data" 'BEGIN { for (i = 0; i < n; i++) printf "/f%d=%s\n", i, src }' >"$list"
"$frontend" --addfiles "$image" "$list"

extents=$("$frontend" --show-metadata --retrieval-pointers "$image" '/$MFT' 0 | grep -vc '^start ')
test "$extents" -eq 1
"$frontend" --check "$image" | grep -q " entries=$((count + 15)) .* errors=0$"
"$frontend" --cat "$image" "/f$((count - 1))" | cmp - "$data"

clusters=$("$frontend" --volume-data "$image" | awk '$1 == "clusters" { print $2 }')
zone_start=$("$frontend" --volume-data "$image" | awk '$1 == "mft-zone-start" { print $2 }')
zone_end=$("$frontend" --volume-data "$image" | awk '$1 == "mft-zone-end" { print $2 }')
test $((zone_end - zone_start)) -eq $((clusters / 8))
mft_end=$("$frontend" --show-metadata --retrieval-pointers "$image" '/$MFT' 0 | awk '$1 != "start" { print $1 + $2 }')
test "$zone_start" -eq "$mft_end"
