#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     ARM64 memory-manager pointer ordering regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
"""Compile the real kernel pointer-read primitive and check its ordering."""
import argparse
import json
import pathlib
import re
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compdb', required=True)
    args = parser.parse_args()
    entries = json.loads(pathlib.Path(args.compdb).read_text())
    entry = next(item for item in entries if item['file'].replace('\\', '/').endswith('/ntos/mm/section/section.c'))
    original = shlex.split(entry['command'])
    command = []
    index = 0
    while index < len(original):
        word = original[index]
        if word in ('-MT', '-MF', '-o'):
            index += 2
            continue
        if word not in ('-MD', '-c') and word != entry['file']:
            command.append(word)
        index += 1
    with tempfile.TemporaryDirectory(prefix='mm-atomic-pointer-') as directory:
        root = pathlib.Path(directory)
        source = root / 'probe.c'
        assembly = root / 'probe.s'
        source.write_text('#include <mm/env/km/mikm.h>\nPVOID MiProbePublishedPointer(PVOID volatile *Slot)\n{\n    return MI_ATOMIC_READ_POINTER(Slot);\n}\n')
        subprocess.run(command + ['-S', '-o', str(assembly), str(source)], cwd=entry['directory'], check=True)
        text = assembly.read_text()
        match = re.search(r'MiProbePublishedPointer:.*?\.Lfunc_end\d+:', text, re.DOTALL)
        if not match or not re.search(r'\b(?:ldar\w*|dmb\s+ish(?:ld)?)\b', match.group()):
            raise RuntimeError('ARM64 pointer load has no acquire instruction or barrier')
    print('ARM64 pointer load has acquire ordering')


if __name__ == '__main__':
    main()
