#!/bin/sh
# POSIX edge cases through a --writable FUSE mount, then an independent
# check of the volume: ntfsfix, the core checker, and NTFS-3G reading back
# the tree the mount wrote.
set -eu

frontend=$1
edge=$2
mkntfs=$3
ntfs3g=$4
fusermount3=$5
ntfsfix=$6

test -e /dev/fuse || exit 77

runtime_root=${NTFSLIB_BENCH_ROOT:-${TMPDIR:-/tmp}}
workdir=$(mktemp -d "$runtime_root/ntfslib-posix-edge.XXXXXX")
image="$workdir/edge.ntfs"
mountpoint="$workdir/mnt"
checkpoint="$workdir/ntfs3g"
mounted=0
checkmounted=0

unmount_path()
{
    "$fusermount3" -u "$1" 2>/dev/null || umount "$1"
}

cleanup()
{
    if test "$checkmounted" -eq 1; then
        unmount_path "$checkpoint" >/dev/null 2>&1 || true
    fi
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

mkdir "$mountpoint" "$checkpoint"
truncate -s 384M "$image"
"$mkntfs" -F -Q -q -L POSIXEDGE "$image"

"$frontend" --writable "$image" "$mountpoint" || exit 77
mounted=1
wait_mount "$mountpoint" || exit 77
mkdir "$mountpoint/t"
POSIX_EDGE_SERIALIZED_FS=1 "$edge" "$mountpoint/t"
unmount_path "$mountpoint"
mounted=0

"$ntfsfix" -n "$image" >/dev/null
"$frontend" --check "$image" | grep -q ' errors=0$'

# Windows records a resident stream's allocation as its length rounded up to 8.
for dir in /t/dupsize /t/dupsize/sub; do
    "$frontend" --list-info "$image" "$dir" | awk -v dir="$dir" '
        $1 == "-" {
            size = $2; alloc = $3; rounded = int((size + 7) / 8) * 8
            if (alloc != rounded && (alloc % 4096 != 0 || alloc < size)) {
                printf "%s/%s: size %d allocated %d\n", dir, $4, size, alloc
                bad = 1
            }
            n++
        }
        END { exit bad || n == 0 }' >&2
done
# Every link's index entry follows a size change, wherever its $FILE_NAME lives.
"$frontend" --list-info "$image" /t/manylinks | awk '
    $1 == "-" { n++; if ($2 != 8000 || $3 != 8192) { print "manylinks/" $4 ": " $2 " " $3; bad = 1 } }
    END { exit bad || n != 41 }' >&2
"$frontend" --list-info "$image" /t/extdata | awk '
    $1 == "-" { n++; if ($2 != 262144 || $3 != 262144) { print "extdata: " $2 " " $3; bad = 1 } }
    END { exit bad || n != 2 }' >&2

"$ntfs3g" "$image" "$checkpoint" -o ro
checkmounted=1
wait_mount "$checkpoint"
"$edge" --verify "$checkpoint/t"
unmount_path "$checkpoint"
checkmounted=0

echo "POSIX edge cases and independent read-back passed"
