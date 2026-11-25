#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Files of an NTFS volume read from the WIM that backs them and leave it at the first write
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import hashlib
import pathlib
import random
import struct
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--ntfsimg', required=True)
parser.add_argument('--wimage', required=True)
args = parser.parse_args()

CLUSTER = 4096
WOF_TAG = 0x80000017
failures = 0


def check(condition, what):
    global failures
    if not condition:
        failures += 1
        print('FAIL: ' + what)


def tool(*arguments):
    return subprocess.run([args.ntfsimg] + [str(argument) for argument in arguments], capture_output=True)


def value(image, name):
    for line in tool('--volume-data', image).stdout.decode().splitlines():
        if line.startswith(name + ' '):
            return int(line.split()[1])
    return None


def info(image, path):
    parent, _, leaf = path.rpartition('/')
    for line in tool('--list-info', image, parent or '/').stdout.decode().splitlines():
        fields = line.split(None, 3)
        if len(fields) == 4 and fields[3].rsplit(' ', 2)[0] == leaf:
            return int(fields[1]), int(fields[2])
    return None


def pointer(source, size, stored, offset):
    return (struct.pack('<IHH', WOF_TAG, 88, 0) + struct.pack('<IIII', 1, 1, 2, 0) + struct.pack('<Q', source) +
            bytes(40) + struct.pack('<QQQ', size, stored, offset))


generator = random.Random(1)
text = b''.join(b'line %08d of a file that compresses well\r\n' % index for index in range(5000))
FILES = {
    'empty.txt': b'',
    'tiny.txt': b'a short text kept in the record\n',
    'mid.bin': bytes(generator.randrange(256) for _ in range(700)),
    'page.bin': bytes(generator.randrange(4) for _ in range(CLUSTER)),
    'text.txt': text,
    'copy of text.txt': text,
    'random.bin': bytes(generator.randrange(256) for _ in range(150000)),
    'Program Files/Common Files/a b.dll': bytes(generator.randrange(16) for _ in range(70000)),
    'Windows/System32/deep/nested.sys': b'MZ' + bytes(9000),
}
DIRECTORIES = ['hollow', 'Windows/TEMP']

with tempfile.TemporaryDirectory() as temporary:
    work = pathlib.Path(temporary)
    tree = work / 'tree'
    for path, data in FILES.items():
        (tree / path).parent.mkdir(parents=True, exist_ok=True)
        (tree / path).write_bytes(data)
    for path in DIRECTORIES:
        (tree / path).mkdir(parents=True)

    wim = work / 'test.wim'
    result = subprocess.run([args.wimage, 'capture', str(tree), str(wim), 'LiberNT', '--compress=xpress', '--boot'],
                            capture_output=True)
    check(result.returncode == 0, 'the tree is captured: ' + result.stderr.decode().strip())

    image = work / 'volume.ntfs'
    check(tool('--format', image, 65536, 2048, 8, 'WIMTEST').returncode == 0, 'the volume is formatted')
    result = tool('--addwim', image, wim, 'sources/boot.wim', 1048576, '0x1234abcd')
    check(result.returncode == 0, 'the WIM is added to the volume: ' + result.stderr.decode().strip())

    check(tool('--cat', image, '/sources/boot.wim').stdout == wim.read_bytes(), 'the volume holds the WIM unchanged')
    overlay = tool('--cat', image, '/System Volume Information/WimOverlay.dat').stdout
    check(overlay[:8] == struct.pack('<II', 0x66436F57, 1) and wim.read_bytes()[24:40] in overlay,
          'the overlay file registers the WIM by its GUID')
    for path, data in FILES.items():
        result = tool('--cat', image, '/' + path)
        check(result.returncode == 0 and result.stdout == data, path + ' reads its content from the WIM')
        size = info(image, path)
        check(size is not None and size[0] == len(data), path + ' has its size in the directory: %r' % (size,))
        if len(data) > 2 * CLUSTER:
            check(size is not None and size[1] == 0, path + ' holds no cluster of its own: %r' % (size,))
        reparse = tool('--reparse', image, '/' + path)
        check((reparse.returncode == 0) == (len(data) != 0), path + ' is a pointer file unless it is empty')
        if len(data) != 0:
            check(len(reparse.stdout) == 96 and reparse.stdout[:16] == pointer(0, 0, 0, 0)[:16] and
                  reparse.stdout[32:52] == hashlib.sha1(data).digest() and
                  struct.unpack('<Q', reparse.stdout[72:80])[0] == len(data),
                  path + ' points at its data by hash and size')
    for path in DIRECTORIES:
        check(tool('--list', image, '/' + path).returncode == 0, path + ' exists as an empty directory')
    result = tool('--cat-range', image, '/text.txt', 65000, 2000)
    check(result.stdout == text[65000:67000], 'a read across two chunks returns the bytes of both')
    result = tool('--cat-range', image, '/random.bin', 149000, 5000)
    check(result.stdout == FILES['random.bin'][149000:], 'a read past the end of a stored file stops at its size')
    check(tool('--addwim', image, wim, 'sources/second.wim', 1048576, '0x1234abcd').returncode != 0,
          'a second WIM is refused on a volume that has one')

    patch = work / 'patch.bin'
    patch.write_bytes(b'WRITTEN-AFTER-BOOT')
    for path, offset in (('text.txt', 100), ('mid.bin', 3), ('tiny.txt', 0), ('random.bin', 149990)):
        expected = bytearray(FILES[path])
        expected[offset:offset + 18] = patch.read_bytes()
        free = value(image, 'free-clusters')
        result = tool('--write', image, '/' + path, offset, patch)
        check(result.returncode == 0, path + ' accepts a write: ' + result.stderr.decode().strip())
        check(tool('--cat', image, '/' + path).stdout == bytes(expected), path + ' holds its old content with the write')
        check(tool('--reparse', image, '/' + path).returncode != 0, path + ' no longer points at the WIM')
        if len(expected) > 2 * CLUSTER:
            clusters = (len(expected) + CLUSTER - 1) // CLUSTER
            check(free - value(image, 'free-clusters') == clusters,
                  path + ' takes %d clusters of the volume once written' % clusters)
    check(tool('--cat', image, '/copy of text.txt').stdout == text, 'a file that shares the written data keeps the original')

    check(tool('--truncate', image, '/Program Files/Common Files/a b.dll', 1000).returncode == 0, 'a pointer file is truncated')
    check(tool('--cat', image, '/Program Files/Common Files/a b.dll').stdout == FILES['Program Files/Common Files/a b.dll'][:1000],
          'a truncated pointer file keeps the start of its content')
    check(tool('--remove', image, '/page.bin').returncode == 0, 'a pointer file is deleted')
    check(tool('--cat', image, '/page.bin').returncode != 0, 'a deleted pointer file is gone')
    check(tool('--cat', image, '/Windows/System32/deep/nested.sys').stdout == FILES['Windows/System32/deep/nested.sys'],
          'a file nothing wrote to still reads from the WIM')
    check(tool('--cat', image, '/sources/boot.wim').stdout == wim.read_bytes(), 'the WIM is unchanged by the writes')

    stray = work / 'stray.bin'
    stray.write_bytes(pointer(7, 5000, 5000, 208))
    check(tool('--create-file', image, '/stray.bin').returncode == 0 and
          tool('--set-sparse', image, '/stray.bin', 1).returncode == 0 and
          tool('--truncate', image, '/stray.bin', 5000).returncode == 0 and
          tool('--set-reparse', image, '/stray.bin', stray).returncode == 0, 'a pointer to an unknown WIM is stored')
    check(tool('--cat', image, '/stray.bin').returncode != 0, 'a pointer to an unknown WIM does not read as zeros')
    beyond = work / 'beyond.bin'
    beyond.write_bytes(pointer(0, 5000, 5000, wim.stat().st_size - 100))
    check(tool('--create-file', image, '/beyond.bin').returncode == 0 and
          tool('--set-sparse', image, '/beyond.bin', 1).returncode == 0 and
          tool('--truncate', image, '/beyond.bin', 5000).returncode == 0 and
          tool('--set-reparse', image, '/beyond.bin', beyond).returncode == 0, 'a pointer past the end of the WIM is stored')
    check(tool('--cat', image, '/beyond.bin').returncode != 0, 'a pointer past the end of the WIM is not read')

    result = tool('--check', image)
    check(result.returncode == 0 and b'errors=0' in result.stdout, 'the volume is consistent: ' + result.stdout.decode().strip())

    plain = work / 'plain.ntfs'
    check(tool('--format', plain, 16384, 2048, 8, 'PLAIN').returncode == 0, 'a plain volume is formatted')
    data = work / 'data.bin'
    data.write_bytes(FILES['mid.bin'])
    tag = work / 'tag.bin'
    tag.write_bytes(struct.pack('<IHH', 0x80000042, 88, 0) + bytes(range(88)))
    check(tool('--create-file', plain, '/full.bin').returncode == 0 and
          tool('--write', plain, '/full.bin', 0, data).returncode == 0, 'a file fills its record with data')
    result = tool('--set-reparse', plain, '/full.bin', tag)
    check(result.returncode == 0, 'a reparse point fits a file whose data filled its record: ' + result.stderr.decode().strip())
    check(tool('--reparse', plain, '/full.bin').stdout == tag.read_bytes(), 'the reparse point reads back')
    check(tool('--cat', plain, '/full.bin').stdout == FILES['mid.bin'], 'the data that left the record is intact')

    check(tool('--create-file', plain, '/sparse.bin').returncode == 0 and
          tool('--set-sparse', plain, '/sparse.bin', 1).returncode == 0 and
          tool('--truncate', plain, '/sparse.bin', 1048576).returncode == 0, 'an empty sparse file grows')
    check(info(plain, 'sparse.bin') == (1048576, 0), 'an empty sparse file grows without a cluster: %r' % (info(plain, 'sparse.bin'),))
    check(tool('--allocated-ranges', plain, '/sparse.bin', 0, 1048576).stdout.strip() == b'', 'it reports no allocated range')
    check(tool('--cat', plain, '/sparse.bin').stdout == bytes(1048576), 'it reads as zeros')
    check(tool('--create-file', plain, '/kept.bin').returncode == 0 and
          tool('--write', plain, '/kept.bin', 0, patch).returncode == 0 and
          tool('--set-sparse', plain, '/kept.bin', 1).returncode == 0 and
          tool('--truncate', plain, '/kept.bin', 1048576).returncode == 0, 'a sparse file with data grows')
    check(tool('--cat-range', plain, '/kept.bin', 0, 18).stdout == patch.read_bytes(), 'a sparse file that had data keeps it')
    result = tool('--check', plain)
    check(result.returncode == 0 and b'errors=0' in result.stdout, 'the plain volume is consistent')

print('%s: %d failure(s)' % ('FAIL' if failures else 'PASS', failures))
sys.exit(failures != 0)
