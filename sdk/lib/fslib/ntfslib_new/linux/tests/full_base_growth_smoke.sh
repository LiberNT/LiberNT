#!/bin/sh
#
# PROJECT:     LiberNT NTFS library tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Grow a fragmented stream whose full base record has nothing else to move
# COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
#
set -eu

driver=$1
mkntfs=$2
ntfscp=$3
ntfscat=$4
ntfsfix=$5

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-fullbase.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/fullbase.ntfs"
cluster="$workdir/cluster.bin"
frame="$workdir/frame.bin"
expected="$workdir/expected.bin"
target=/transaction-log.db-wal

truncate -s 16M "$image"
"$mkntfs" -F -Q -q -c 4096 "$image" 2>/dev/null
dd if=/dev/zero bs=4096 count=1 status=none | tr '\000' C >"$cluster"

files=0
while [ $files -lt 4000 ] &&
      "$ntfscp" -f -q "$image" "$cluster" "/c$files.bin" 2>/dev/null; do
    files=$((files + 1))
done
test "$("$driver" --probe "$image" | awk '/^free clusters:/ { print $3 }')" -eq 0
i=0
while [ $i -lt $files ]; do
    "$driver" --truncate "$image" "/c$i.bin" 0 >/dev/null
    i=$((i + 2))
done

# The name and its 8.3 alias leave the base record full with only $DATA to move.
"$driver" --short-names --create-file "$image" "$target" >/dev/null
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

extents=$("$driver" --retrieval-pointers "$image" "$target" 0 | grep -c .)
test "$extents" -gt 170
"$driver" --cat "$image" "$target" | cmp - "$expected"
"$ntfscat" "$image" "$target" | cmp - "$expected"
"$driver" --check "$image" | grep -q ' errors=0$'
"$ntfsfix" -n "$image" >/dev/null

half=$((offset / 2))
"$driver" --truncate "$image" "$target" $half >/dev/null
head -c $half "$expected" >"$expected.half"
mv "$expected.half" "$expected"
"$driver" --cat "$image" "$target" | cmp - "$expected"
"$driver" --check "$image" | grep -q ' errors=0$'

"$driver" --truncate "$image" "$target" $((half + 8192)) >/dev/null
dd if=/dev/zero bs=8192 count=1 status=none >>"$expected"
"$driver" --cat "$image" "$target" | cmp - "$expected"
"$driver" --check "$image" | grep -q ' errors=0$'

"$driver" --truncate "$image" "$target" 0 >/dev/null
: >"$expected"
offset=0
i=0
while [ $i -lt 64 ]; do
    dd if=/dev/urandom bs=4120 count=1 status=none >"$frame"
    "$driver" --write "$image" "$target" $offset "$frame" >/dev/null
    cat "$frame" >>"$expected"
    offset=$((offset + 4120))
    i=$((i + 1))
done
"$driver" --cat "$image" "$target" | cmp - "$expected"
"$ntfscat" "$image" "$target" | cmp - "$expected"
"$driver" --check "$image" | grep -q ' errors=0$'
"$ntfsfix" -n "$image" >/dev/null
