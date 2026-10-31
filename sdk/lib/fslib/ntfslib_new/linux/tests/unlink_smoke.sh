#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfsls=$3
ntfsinfo=$4
ntfsfix=$5

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-unlink.XXXXXX")
image="$workdir/unlink.ntfs"
payload="$workdir/payload.bin"
trap 'rm -r -- "$workdir"' EXIT HUP INT TERM

free_clusters()
{
    "$driver" --probe "$image" |
        awk '/^free clusters:/ { print $3 }'
}

record_links()
{
    "$ntfsinfo" -i $(($1 & 0xffffffffffff)) "$image" 2>/dev/null |
        awk '/Number of Hard Links/ { print $5 }'
}

visible()
{
    "$ntfsls" -p "$1" "$image" | grep -qx "$2"
}

truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L UNLINK "$image"
baseline=$(free_clusters)

# The last name goes while the record and its clusters stay allocated.
"$driver" --create-dir "$image" /dir >/dev/null
"$driver" --create-file "$image" /dir/data.bin >/dev/null
dd if=/dev/urandom of="$payload" bs=4096 count=8 status=none
"$driver" --write "$image" /dir/data.bin 0 "$payload"
before=$(free_clusters)
ref=$("$driver" --unlink "$image" /dir/data.bin)
test "$(free_clusters)" -eq "$before"
if visible /dir data.bin; then
    echo "unlinked name still visible" >&2
    exit 1
fi
test "$(record_links "$ref")" -eq 0

# The name is free for a new file while the unlinked record lives.
"$driver" --create-file "$image" /dir/data.bin >/dev/null
"$driver" --remove "$image" /dir/data.bin
test "$(record_links "$ref")" -eq 0

# Releasing by reference returns the record and its clusters, once.
"$driver" --delete-unlinked "$image" "$ref"
test $(($(free_clusters) - before)) -eq 8
if "$driver" --delete-unlinked "$image" "$ref" 2>/dev/null; then
    echo "released record was deleted twice" >&2
    exit 1
fi

# A record that still has a name is not released by reference.
"$driver" --create-file "$image" /dir/a.bin >/dev/null
"$driver" --link "$image" /dir/a.bin /dir/b.bin
ref=$("$driver" --unlink "$image" /dir/a.bin)
test "$(record_links "$ref")" -eq 1
if "$driver" --delete-unlinked "$image" "$ref" 2>/dev/null; then
    echo "record with a remaining name was released" >&2
    exit 1
fi
visible /dir b.bin
"$driver" --remove "$image" /dir/b.bin

# An empty directory is unlinked and released the same way.
"$driver" --create-dir "$image" /dir/sub >/dev/null
ref=$("$driver" --unlink-dir "$image" /dir/sub)
if visible /dir sub; then
    echo "unlinked directory still visible" >&2
    exit 1
fi
"$driver" --delete-unlinked "$image" "$ref"
"$driver" --remove-dir "$image" /dir

test "$(free_clusters)" -le "$baseline"
"$ntfsfix" -n "$image" >/dev/null
