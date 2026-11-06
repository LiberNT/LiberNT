#!/bin/sh
#
# PROJECT:     LiberNT NTFS library tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Keep --addfiles directories case-insensitive and --add-lx ones case-sensitive
# COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
#
set -eu

driver=$1

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-importcase.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/import.ntfs"

truncate -s 32M "$image"
"$driver" --format "$image" 65536 0 8 IMPORT >/dev/null

printf 'payload\n' >"$workdir/ntdll.dll"
printf 'Windows/System32/ntdll.dll=%s\n' "$workdir/ntdll.dll" >"$workdir/files.lst"
"$driver" --addfiles "$image" "$workdir/files.lst"

printf 'd\t755\t0\t0\t0\t0\t0\t/usr\t\n' >"$workdir/lx.lst"
printf 'd\t755\t0\t0\t0\t0\t0\t/usr/bin\t\n' >>"$workdir/lx.lst"
printf 'f\t755\t0\t0\t0\t0\t0\t/usr/bin/head\t%s\n' "$workdir/ntdll.dll" >>"$workdir/lx.lst"
"$driver" --add-lx "$image" "$workdir/lx.lst"

"$driver" --cat "$image" /Windows/System32/ntdll.dll | grep -q '^payload$'
"$driver" --cat "$image" /WINDOWS/system32/NTDLL.DLL | grep -q '^payload$'
"$driver" --cat "$image" /usr/bin/head | grep -q '^payload$'
"$driver" --cat "$image" /USR/bin/head | grep -q '^payload$'
if "$driver" --cat "$image" /usr/BIN/head >/dev/null 2>&1; then
    exit 1
fi
if "$driver" --cat "$image" /usr/bin/HEAD >/dev/null 2>&1; then
    exit 1
fi
