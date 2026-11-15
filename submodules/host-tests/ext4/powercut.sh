#!/bin/sh
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Cuts the ext4 core stress at chosen device writes and checks what journal recovery leaves, by e2fsck
#              and by the core itself
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# usage: powercut.sh TOOL LAYOUT
TOOL="$1"; LAYOUT="$2"
PATH="$PATH:/usr/sbin:/sbin"
command -v mkfs.ext4 > /dev/null && command -v e2fsck > /dev/null || exit 77
# REMOUNT 0 never checkpoints on its own: with the small journal the log fills and wraps
case "$LAYOUT" in
    default) ARGS=""; SEED=31; STEPS=1500; REMOUNT=200 ;;
    bs1k) ARGS="-b 1024"; SEED=42; STEPS=1500; REMOUNT=170 ;;
    smalljournal) ARGS="-J size=4"; SEED=41; STEPS=2500; REMOUNT=0 ;;
    *) exit 2 ;;
esac
DIR="$(mktemp -d)" || exit 2
trap 'rm -rf "$DIR"' EXIT
cd "$DIR" || exit 2
truncate -s 128M base && mkfs.ext4 -q -F $ARGS base 2> /dev/null || exit 2
BAD=0
CUT=37
while [ $CUT -lt 4800 ]; do
    cp --sparse=always base cut
    E4_NAME=cut E4_FAIL_AFTER=$CUT "$TOOL" cut stress $SEED $STEPS $REMOUNT > /dev/null 2>&1
    [ $? -eq 77 ] || break
    cp --sparse=always cut replayed
    # e2fsck replays the journal and releases the orphans. The free counts of the superblock are not journalled,
    # and a hint about the shape of an extent tree is not damage; anything else it reports is.
    # FIXME: a link is created as an empty one and gets its target in a second transaction; a cut between the two
    # leaves a link e2fsck calls invalid
    e2fsck -fy cut > fsck.out 2>&1
    if grep -v '^e2fsck\|^Pass\|recovering journal\|^Free blocks count wrong\|^Free inodes count wrong\|^Fix\|^$' fsck.out |
       grep -v 'FILE SYSTEM WAS MODIFIED\|files (\|^Clearing orphaned\|could be shorter\|could be narrower\|^Symlink .* is invalid\|^Clear' |
       grep -q .; then
        echo "cut at write $CUT: e2fsck had to repair the volume"; cat fsck.out; BAD=1
    fi
    # the core replays the journal and releases the orphans itself: e2fsck then finds nothing
    "$TOOL" replayed ls / > /dev/null 2>&1 || { echo "cut at write $CUT: the core cannot mount the volume"; BAD=1; }
    e2fsck -fn replayed > fsck.out 2>&1 || { echo "cut at write $CUT: damage after the core's recovery"; cat fsck.out; BAD=1; }
    CUT=$((CUT + 113))
done
exit $BAD
