#!/bin/sh
# A volume is opened for writing only when its state is known to be clean:
# an LFS 1.x log, which is not replayed, must say its volume was closed
# cleanly, and a hibernated system keeps its volume read-only.
set -eu

driver=$1
mkntfs=$2
ntfsinfo=$3
ntfscp=$4

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-dirtylog.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

state() {
    "$driver" --recover "$1" 2>/dev/null || true
}

make_image() {
    truncate -s 32M "$1"
    "$mkntfs" -F -Q -q "$1" >/dev/null 2>&1
}

# Write two LFS restart pages of version 1.1 at the start of $LogFile
write_log() {
    offset=$("$ntfsinfo" -f -v -i 2 "$1" 2>/dev/null | awk '/Runlist:/ { getline; print $2; exit }')
    cluster=$("$ntfsinfo" -f -m "$1" 2>/dev/null | awk '/Cluster Size:/ { print $3; exit }')
    python3 - "$1" "$offset" "$cluster" "$2" <<'PY'
import struct, sys
image, lcn, cluster, flags = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3]), int(sys.argv[4])
page = 4096
def restart_page():
    p = bytearray(page)
    p[0:4] = b'RSTR'
    struct.pack_into('<HH', p, 4, 0x1e, page // 512 + 1)
    struct.pack_into('<IIHHH', p, 0x10, page, page, 0x30, 1, 1)
    area = 0x30
    struct.pack_into('<QHHHHI', p, area, 0x1000, 1, 0xffff, 0, flags, 44)
    struct.pack_into('<HHQ', p, area + 0x14, 0x30, 0x30, 2 * 1024 * 1024)
    usn = 1
    struct.pack_into('<H', p, 0x1e, usn)
    for i in range(page // 512):
        end = (i + 1) * 512 - 2
        p[0x20 + 2 * i:0x22 + 2 * i] = p[end:end + 2]
        struct.pack_into('<H', p, end, usn)
    return p
with open(image, 'r+b') as f:
    f.seek(lcn * cluster)
    f.write(restart_page() + restart_page())
PY
}

image="$workdir/clean.ntfs"
make_image "$image"
result=$(state "$image")
[ "$result" = writable ] || { echo "blank log: $result, expected writable"; exit 1; }

write_log "$image" 2
result=$(state "$image")
[ "$result" = writable ] || { echo "clean 1.1 log: $result, expected writable"; exit 1; }

image="$workdir/dirty.ntfs"
make_image "$image"
write_log "$image" 0
result=$(state "$image")
[ "$result" = read-only ] || { echo "unclean 1.1 log: $result, expected read-only"; exit 1; }

image="$workdir/hibernated.ntfs"
make_image "$image"
printf 'hibr' > "$workdir/hiberfil.sys"
"$ntfscp" -f -q "$image" "$workdir/hiberfil.sys" /hiberfil.sys
result=$(state "$image")
[ "$result" = read-only ] || { echo "hibernated: $result, expected read-only"; exit 1; }

echo "dirty log read-only smoke passed"
