#!/bin/sh
#
# PROJECT:     LiberNT NTFS library tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Grow a directory index by a new run while its root fills the base file record
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
#
set -eu

frontend=$1
formatter=$2

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-index-record-full.XXXXXX")
trap 'rm -r -- "$workdir"' EXIT HUP INT TERM
image="$workdir/test.ntfs"
count=331

"$formatter" "$image" 64 >/dev/null 2>&1
"$frontend" --create-dir "$image" /f >/dev/null
"$frontend" --short-names --create-dir "$image" /d >/dev/null
head -c 4096 /dev/zero | tr '\0' 'x' > "$workdir/cluster.bin"

# 81-character names promote 256-byte keys into the resident root, which
# fills the directory's base record in one step; the one-cluster files in
# between keep every new index block in a run of its own.
plug=0
index=0
while test "$index" -lt "$count"; do
    if test $((index % 19)) -eq 0 ||
       { test "$index" -ge 190 && test $((index % 5)) -eq 0; }; then
        plug=$((plug + 1))
        "$frontend" --create-file "$image" "/f/p$plug" >/dev/null
        "$frontend" --write "$image" "/f/p$plug" 0 "$workdir/cluster.bin" >/dev/null
    fi
    name=$(printf '%081d' "$index")
    if ! "$frontend" --short-names --create-file "$image" "/d/$name" >/dev/null; then
        echo "creating entry $index failed" >&2
        exit 1
    fi
    index=$((index + 1))
done

listed=$("$frontend" --list "$image" /d | wc -l)
if test "$listed" -ne "$count"; then
    echo "listed $listed of $count entries" >&2
    exit 1
fi
summary=$("$frontend" --check "$image" | tail -n 1)
case "$summary" in
    *" errors=0") ;;
    *) echo "check: $summary" >&2; exit 1 ;;
esac
