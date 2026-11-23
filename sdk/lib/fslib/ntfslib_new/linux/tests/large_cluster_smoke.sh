#!/bin/sh
#
# PROJECT:     LiberNT NTFS library tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Directories on volumes whose clusters are larger than an index record
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#
set -eu

driver=$1
mkntfs=$2
ntfscat=$3
ntfsfix=$4

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-large-cluster.XXXXXX")
trap 'rm -r -- "$workdir"' EXIT HUP INT TERM

# Byte 0x0C of the $I30 INDEX_ROOT value: Windows 11 writes the index record size
# in clusters, or in 512-byte index VCN units when the record is smaller than a cluster.
index_root_units()
{
    "$ntfscat" -i "$2" -a 0x90 -n '$I30' "$1" |
        dd bs=1 skip=12 count=1 status=none |
        od -An -tu1 |
        tr -d ' '
}

expect_units()
{
    _units=$(index_root_units "$1" "$2")
    if test "$_units" != "$3"; then
        echo "$1 record $2: INDEX_ROOT index size $_units, expected $3" >&2
        exit 1
    fi
}

check_volume()
{
    _image=$1
    _units=$2

    expect_units "$_image" 5 "$_units"
    "$driver" --create-file "$_image" /alpha.txt >/dev/null
    _dir=$("$driver" --create-dir "$_image" /Boot)
    "$driver" --create-file "$_image" /Boot/BCD.LOG1 >/dev/null
    expect_units "$_image" "$_dir" "$_units"
    "$ntfsfix" -n "$_image" >/dev/null
}

for cluster in 4096 8192 65536; do
    image="$workdir/mkntfs-$cluster.ntfs"
    truncate -s 128M "$image"
    "$mkntfs" -F -Q -q -c "$cluster" "$image"
    check_volume "$image" $((cluster > 4096 ? 8 : 1))

    image="$workdir/format-$cluster.ntfs"
    truncate -s 128M "$image"
    "$driver" --format "$image" 262144 0 $((cluster / 512)) LARGE >/dev/null
    check_volume "$image" $((cluster > 4096 ? 8 : 1))
done
