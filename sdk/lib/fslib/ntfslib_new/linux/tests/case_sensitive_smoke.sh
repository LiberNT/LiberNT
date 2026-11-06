#!/bin/sh
#
# PROJECT:     LiberNT NTFS library tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Keep image directories case-insensitive outside imported Linux trees
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#
set -eu

driver=$1

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-case.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/case.ntfs"

truncate -s 32M "$image"
"$driver" --format "$image" 65536 0 8 CASE

printf 'windows\n' >"$workdir/windows.txt"
printf 'linux\n' >"$workdir/linux.txt"
printf 'Windows/system32/probe.txt=%s\n' "$workdir/windows.txt" >"$workdir/files.lst"
printf 'd\t755\t0\t0\t0\t0\t0\tusr\t\n' >"$workdir/linux.lst"
printf 'd\t755\t0\t0\t0\t0\t0\tusr/bin\t\n' >>"$workdir/linux.lst"
printf 'f\t644\t0\t0\t0\t0\t0\tusr/bin/head\t%s\n' "$workdir/linux.txt" >>"$workdir/linux.lst"

"$driver" --addfiles "$image" "$workdir/files.lst"
"$driver" --add-lx "$image" "$workdir/linux.lst"

test "$("$driver" --cat "$image" Windows/system32/probe.txt)" = windows
test "$("$driver" --cat "$image" WINDOWS/System32/PROBE.TXT)" = windows
test "$("$driver" --cat "$image" usr/bin/head)" = linux
if "$driver" --cat "$image" usr/BIN/head >/dev/null 2>&1; then
    exit 1
fi
