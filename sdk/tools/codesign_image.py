#!/usr/bin/env python3
#
# PROJECT:     LiberNT build tools
# LICENSE:     MIT (https://spdx.org/licenses/MIT)
# PURPOSE:     Signs the unsigned PE files that an image file list takes from the build tree
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

import argparse
import fcntl
import os
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor


def needs_signature(path):
    try:
        with open(path, 'rb') as f:
            dos = f.read(0x40)
            if len(dos) < 0x40 or dos[:2] != b'MZ':
                return False
            f.seek(struct.unpack_from('<I', dos, 0x3C)[0])
            if f.read(4) != b'PE\0\0':
                return False
            coff = f.read(20)
            if len(coff) < 20:
                return False
            optional = f.read(struct.unpack_from('<H', coff, 16)[0])
            if len(optional) < 2:
                return False
            magic = struct.unpack_from('<H', optional, 0)[0]
            directories = 96 if magic == 0x10B else 112 if magic == 0x20B else None
            if directories is None or len(optional) < directories + 5 * 8:
                return False
            return struct.unpack_from('<II', optional, directories + 4 * 8)[1] == 0
    except OSError:
        return False


def sign(args, path):
    temp = '%s.%d.signing' % (path, os.getpid())
    result = subprocess.run([args.osslsigncode, 'sign', '-certs', args.cert, '-key', args.key,
                             '-n', 'LiberNT', '-h', 'sha256', '-in', path, '-out', temp],
                            capture_output=True, text=True)
    if result.returncode != 0:
        if os.path.exists(temp):
            os.remove(temp)
        return path, result.stdout + result.stderr
    os.replace(temp, path)
    return path, None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--osslsigncode', required=True)
    parser.add_argument('--cert', required=True)
    parser.add_argument('--key', required=True)
    parser.add_argument('--root', required=True)
    parser.add_argument('lists', nargs='+')
    args = parser.parse_args()

    root = os.path.realpath(args.root) + os.sep
    files = set()
    for listfile in args.lists:
        with open(listfile, encoding='utf-8', errors='replace') as f:
            for line in f:
                line = line.rstrip('\r\n')
                if '=' not in line:
                    continue
                source = os.path.realpath(line.split('=', 1)[1])
                if source.startswith(root) and os.path.isfile(source):
                    files.add(source)

    with open(os.path.join(root, '.codesign.lock'), 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        pending = sorted(path for path in files if needs_signature(path))
        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as executor:
            failures = [(path, error) for path, error in executor.map(lambda p: sign(args, p), pending) if error]

    for path, error in failures:
        sys.stderr.write('codesign_image: cannot sign %s\n%s\n' % (path, error))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
