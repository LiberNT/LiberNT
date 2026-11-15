#!/bin/sh
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Formats an image with mkfs.ext4, runs the ext4 core stress on it and checks the result with e2fsck
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# usage: stress.sh TOOL LAYOUT
TOOL="$1"; LAYOUT="$2"
PATH="$PATH:/usr/sbin:/sbin"
command -v mkfs.ext4 > /dev/null && command -v e2fsck > /dev/null || exit 77
case "$LAYOUT" in
    default) ARGS="" ;;
    nojournal) ARGS="-O ^has_journal" ;;
    nocsum) ARGS="-O ^metadata_csum,^metadata_csum_seed" ;;
    ext3) ARGS="-t ext3" ;;
    ext2) ARGS="-t ext2" ;;
    bs1k) ARGS="-b 1024" ;;
    *) exit 2 ;;
esac
DIR="$(mktemp -d)" || exit 2
trap 'rm -rf "$DIR"' EXIT
cd "$DIR" || exit 2
truncate -s 128M image && mkfs.ext4 -q -F $ARGS image 2> /dev/null || exit 2
E4_NAME="$LAYOUT" "$TOOL" image stress 7 1500 150 || exit 1
e2fsck -fn image > /dev/null 2>&1 || exit 1
# The volume grows into a partial last group, then shrinks as far as it can; the files must stay what they were.
# A volume without extents only grows.
BLOCKS=$(dumpe2fs -h image 2> /dev/null | sed -n 's/^Block count: *//p')
"$TOOL" image resize $((BLOCKS * 3 + 5000)) -- verify > /dev/null || exit 1
e2fsck -fn image > /dev/null 2>&1 || exit 1
case "$LAYOUT" in
    ext2|ext3) ;;
    *)
        "$TOOL" image resize min -- verify > /dev/null || exit 1
        e2fsck -fn image > /dev/null 2>&1 || exit 1
        ;;
esac
exit 0
