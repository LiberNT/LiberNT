#!/bin/sh
# Streams file data through each cache mode of a --writable mount, then checks the volume and has
# NTFS-3G read every byte back.
set -eu

frontend=$1
stream=$2
mkntfs=$3
ntfs3g=$4
fusermount3=$5
ntfsfix=$6

test -e /dev/fuse || exit 77

runtime_root=${NTFSLIB_BENCH_ROOT:-${TMPDIR:-/tmp}}
workdir=$(mktemp -d "$runtime_root/ntfslib-data-stream.XXXXXX")
image="$workdir/stream.ntfs"
mountpoint="$workdir/mnt"
mounted=0

unmount_path()
{
    "$fusermount3" -u "$1" 2>/dev/null || umount "$1"
}

cleanup()
{
    if test "$mounted" -eq 1; then
        unmount_path "$mountpoint" >/dev/null 2>&1 || true
    fi
    rm -r -- "$workdir"
}
trap cleanup EXIT HUP INT TERM

wait_mount()
{
    attempt=0
    while ! mountpoint -q "$1" && test "$attempt" -lt 50; do
        sleep 0.1
        attempt=$((attempt + 1))
    done
    mountpoint -q "$1"
}

mkdir "$mountpoint"
for mode in "" "--write-through" "--direct-io"; do
    rm -f "$image"
    truncate -s 1G "$image"
    "$mkntfs" -F -Q -q -L STREAM "$image"

    "$frontend" --writable $mode "$image" "$mountpoint" || exit 77
    mounted=1
    wait_mount "$mountpoint" || exit 77
    mkdir "$mountpoint/t"
    "$stream" write "$mountpoint/t"
    unmount_path "$mountpoint"
    mounted=0

    "$ntfsfix" -n "$image" >/dev/null
    "$frontend" --check "$image" | grep -q ' errors=0$'

    "$ntfs3g" "$image" "$mountpoint" -o ro
    mounted=1
    wait_mount "$mountpoint"
    "$stream" verify "$mountpoint/t"
    unmount_path "$mountpoint"
    mounted=0

    # A fresh mount holds none of the files in the page cache: the walk below is read ahead.
    "$frontend" --writable $mode "$image" "$mountpoint"
    mounted=1
    wait_mount "$mountpoint"
    ls -l "$mountpoint/t/small" >/dev/null
    "$stream" churn "$mountpoint/t"
    unmount_path "$mountpoint"
    mounted=0
    "$ntfsfix" -n "$image" >/dev/null
    "$frontend" --check "$image" | grep -q ' errors=0$'
done

echo "data streams passed in every cache mode"
