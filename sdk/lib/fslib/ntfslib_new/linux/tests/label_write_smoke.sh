#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfslabel=$3
ntfsfix=$4

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-label.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/label.ntfs"
long_label=ABCDEFGHIJKLMNOPQRSTUVWXYZ123456

truncate -s 64M "$image"
"$mkntfs" -F -Q -L BEFORE "$image" >/dev/null

"$driver" --set-label "$image" "$long_label"
core_label=$("$driver" --probe "$image" |
    sed -n 's/^label: //p')
reference_label=$("$ntfslabel" "$image")
test "$core_label" = "$long_label"
test "$reference_label" = "$long_label"
"$ntfsfix" -n "$image" >/dev/null

"$driver" --set-label "$image" ""
core_label=$("$driver" --probe "$image" |
    sed -n 's/^label: //p')
reference_label=$("$ntfslabel" "$image")
test -z "$core_label"
test -z "$reference_label"
"$ntfsfix" -n "$image" >/dev/null

# The native formatter writes an empty $VOLUME_NAME for an unlabeled volume,
# as Windows 11 format does, so a label can be set afterwards.
native="$workdir/native.ntfs"
truncate -s 64M "$native"
"$driver" --format "$native" 131072 0 8 "" >/dev/null
"$driver" --set-label "$native" AFTER
test "$("$ntfslabel" "$native")" = AFTER
"$ntfsfix" -n "$native" >/dev/null
