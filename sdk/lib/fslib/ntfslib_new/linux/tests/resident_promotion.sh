#!/bin/sh
set -eu
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
image=$work/volume
truncate -s 32M "$image"
"$3" -FQ -c 4096 -L PROMOTE "$image" >/dev/null 2>&1
"$1" --create-file "$image" /first
printf seed > "$work/prefix"
"$1" --write "$image" /first 0 "$work/prefix"
"$1" --create-dir "$image" /dir
"$1" --link "$image" /first /dir/second
"$2" "$image"
