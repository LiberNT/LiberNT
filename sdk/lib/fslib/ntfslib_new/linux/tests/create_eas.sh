#!/bin/sh
# Create-time EAs on a fresh volume, then the core checker over it.
set -eu
work=$(mktemp -d "${NTFSLIB_BENCH_ROOT:-${TMPDIR:-/tmp}}/ntfslib-create-eas.XXXXXX")
trap 'rm -rf "$work"' EXIT
image=$work/volume
truncate -s 32M "$image"
"$3" -FQ -c 4096 -L CREATEEAS "$image" >/dev/null 2>&1
"$2" "$image"
"$1" --check "$image" | grep -q ' errors=0$'
