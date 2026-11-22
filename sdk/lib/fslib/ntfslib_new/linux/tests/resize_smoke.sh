#!/bin/sh
# Grow and shrink volumes made by mkntfs and by the core formatter, with data
# in the tail that a shrink has to move, then grow the last partition of MBR
# and GPT disk images. Every result must pass ntfsfix and the core checker,
# and keep every file byte for byte as NTFS-3G reads it back.
set -eu

frontend=$1
mkntfs=$2
ntfs3g=$3
fusermount3=$4
ntfsfix=$5
sfdisk=${6:-}

test -e /dev/fuse || exit 77

runtime_root=${NTFSLIB_BENCH_ROOT:-${TMPDIR:-/tmp}}
workdir=$(mktemp -d "$runtime_root/ntfslib-resize.XXXXXX")
mnt="$workdir/mnt"
mounted=0

cleanup()
{
    if test "$mounted" -eq 1; then
        "$fusermount3" -u "$mnt" >/dev/null 2>&1 || umount "$mnt" >/dev/null 2>&1 || true
    fi
    rm -r -- "$workdir"
}
trap cleanup EXIT HUP INT TERM
mkdir "$mnt"

mount_ro()
{
    "$ntfs3g" "$1" "$mnt" -o ro || exit 77
    mounted=1
}

mount_rw()
{
    "$ntfs3g" "$1" "$mnt" || exit 77
    mounted=1
}

unmount()
{
    "$fusermount3" -u "$mnt" 2>/dev/null || umount "$mnt"
    mounted=0
}

sums()
{
    mount_ro "$1"
    (cd "$mnt" && find . -type f -print0 | sort -z | xargs -0 -r sha256sum)
    unmount
}

verify()
{
    "$ntfsfix" -n "$1" >/dev/null
    "$frontend" --check "$1" | grep -q ' errors=0$'
    sums "$1" | cmp - "$2"
}

# Fill most of the volume, then free the front so data sits in the tail.
fill_tail()
{
    mount_rw "$1"
    i=0
    while head -c 1048576 /dev/urandom >"$mnt/f$i" 2>/dev/null; do
        i=$((i + 1))
    done
    rm -f "$mnt/f$i"
    j=0
    while [ "$j" -lt $((i * 3 / 4)) ]; do
        rm -f "$mnt/f$j"
        j=$((j + 1))
    done
    mkdir "$mnt/dir"
    k=0
    while [ "$k" -lt 400 ]; do
        printf 'entry %d\n' "$k" >"$mnt/dir/name-$k.txt"
        k=$((k + 1))
    done
    unmount
}

run()
{
    name=$1
    image="$workdir/$name.img"
    shift
    "$@" "$image" >/dev/null 2>&1
    fill_tail "$image"
    sums "$image" >"$workdir/$name.sums"
    size=$(wc -c <"$image" | tr -d ' ')
    sector=$(od -An -tu2 -j11 -N2 "$image" | tr -d ' ')

    cp "$image" "$workdir/grow.img"
    truncate -s $((size * 3 / 2 / 4096 * 4096)) "$workdir/grow.img"
    "$frontend" --grow "$workdir/grow.img" $((size * 3 / 2 / 4096 * 4096 / sector)) >/dev/null
    verify "$workdir/grow.img" "$workdir/$name.sums"

    # Shrinking past the data in the tail requires moving it, and moving
    # $LogFile and $MFTMirr out of the middle of an mkntfs volume.
    target=$((size * 2 / 5 / 4096 * 4096))
    "$frontend" --shrink "$image" $((target / sector)) >/dev/null
    truncate -s "$target" "$image"
    verify "$image" "$workdir/$name.sums"

    # A grow past the end of the image must be refused without change.
    cp "$image" "$workdir/before.img"
    if "$frontend" --grow "$image" $((target * 2 / sector)) >/dev/null 2>&1; then
        echo "$name: grow past the image end succeeded" >&2
        exit 1
    fi
    cmp "$image" "$workdir/before.img"
    rm -f "$image" "$workdir/grow.img" "$workdir/before.img"
}

format_mkntfs()
{
    truncate -s 192M "$1"
    "$mkntfs" -F -Q -q "$@"
}

run mkntfs-4k format_mkntfs
run mkntfs-64k sh -c 'truncate -s 192M "$1"; "$0" -F -Q -q -c 65536 "$1"' "$mkntfs"
run mkntfs-4kn sh -c 'truncate -s 192M "$1"; "$0" -F -Q -q -s 4096 "$1"' "$mkntfs"
run core sh -c 'truncate -s 192M "$1"; "$0" --format "$1" $((192 * 2048)) 0 8 CORE' "$frontend"

if [ -n "$sfdisk" ]; then
    for label in dos gpt; do
        disk="$workdir/disk-$label.img"
        part="$workdir/part.img"
        truncate -s 100M "$disk"
        if [ "$label" = dos ]; then type=7; else type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7; fi
        printf 'label: %s\nstart=2048, size=100000, type=%s\n' "$label" "$type" |
            "$sfdisk" -q "$disk"
        truncate -s $((100000 * 512)) "$part"
        "$mkntfs" -F -Q -q -p 2048 "$part" >/dev/null 2>&1
        fill_tail "$part"
        sums "$part" >"$workdir/part.sums"
        dd if="$part" of="$disk" bs=512 seek=2048 conv=notrunc status=none
        truncate -s 200M "$disk"
        "$frontend" --expand-disk "$disk" 1 >/dev/null
        "$sfdisk" -V "$disk" >/dev/null
        size=$("$sfdisk" -d "$disk" | awk -F'size=' '/size=/ { split($2, a, ","); gsub(/ /, "", a[1]); print a[1] }')
        test "$size" -gt 100000
        dd if="$disk" of="$part" bs=512 skip=2048 count="$size" status=none
        verify "$part" "$workdir/part.sums"
        rm -f "$disk" "$part"
    done
fi

echo "resize smoke passed"
