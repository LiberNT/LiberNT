#!/bin/sh
# One core test on a fresh volume, then the core checker over it.
set -eu
work=$(mktemp -d "${NTFSLIB_BENCH_ROOT:-${TMPDIR:-/tmp}}/ntfslib-fresh.XXXXXX")
trap 'rm -rf "$work"' EXIT
image=$work/volume
truncate -s 32M "$image"
"$3" -FQ -c 4096 -L FRESH "$image" >/dev/null 2>&1
"$2" "$image"
"$1" --check "$image" | grep -q ' errors=0$'
