#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     i386 64-bit atomic read regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
"""Compile the real kernel 64-bit read primitive for i386."""
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
    def compiler_for(suffix):
        entry = next(item for item in entries if item['file'].replace('\\', '/').endswith(suffix))
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
        return entry, command
    entry, command = compiler_for('/ntos/mm/section/section.c')
    with tempfile.TemporaryDirectory(prefix='mm-atomic-read64-') as directory:
        root = pathlib.Path(directory)
        source = root / 'probe.c'
        assembly = root / 'probe.s'
        source.write_text(r'''
#include <mm/include/mm.h>
#include <mm/include/miproc.h>
#include <mm/include/misys.h>
#define CHECK_ALIGNMENT(type, member) _Static_assert(__alignof__(type) >= 8 && __builtin_offsetof(type, member) % 8 == 0, #type "." #member)
CHECK_ALIGNMENT(MI_ADDRESS_SPACE, CommittedPages);
CHECK_ALIGNMENT(MI_ADDRESS_SPACE, PageFileGeneration);
CHECK_ALIGNMENT(MI_ADDRESS_SPACE, AwePages);
CHECK_ALIGNMENT(MI_SYSTEM, CommitLimit);
CHECK_ALIGNMENT(MI_SYSTEM, CommittedPages);
CHECK_ALIGNMENT(MI_PAGEFILE, SlotCount);
CHECK_ALIGNMENT(MI_VAD, CommitCharge);
CHECK_ALIGNMENT(MI_SEGMENT, SizeInBytes);
CHECK_ALIGNMENT(MI_SEGMENT, PageCount);
CHECK_ALIGNMENT(MI_PROCESS, PeakWorkingSet);
CHECK_ALIGNMENT(MI_PROCESS, PeakCommit);
CHECK_ALIGNMENT(MI_SYSTEM_PTES, FreePages);
LONG64 MiProbeRead64(volatile LONG64 *Slot)
{
    return MI_ATOMIC_READ64(Slot);
}
''')
        subprocess.run(command + ['-S', '-o', str(assembly), str(source)], cwd=entry['directory'], check=True)
        text = assembly.read_text()
        match = re.search(r'_?MiProbeRead64:.*?Lfunc_end\d+:', text, re.DOTALL)
        if not match or not re.search(r'\block\s+cmpxchg8b\b', match.group()):
            raise RuntimeError('i386 64-bit memory-manager read is not an atomic compare-exchange')
        entry, command = compiler_for('/ntos/mm/pool/va/varegion.c')
        source.write_text(r'''
#include <mm/pool/include/poolenv.h>
#include <mm/pool/include/poolva.h>
#include <mm/pool/include/pooltrack.h>
_Static_assert(__alignof__(POOL_VA_REGION) >= 8 &&
               __builtin_offsetof(POOL_VA_REGION, CommittedPages) % 8 == 0 &&
               __builtin_offsetof(POOL_VA_REGION, PeakCommittedPages) % 8 == 0,
               "pool counters must be aligned for atomic access");
_Static_assert(__alignof__(POOL_TAG_ENTRY) >= 8 &&
               __builtin_offsetof(POOL_TAG_ENTRY, Bytes) % 8 == 0,
               "pool tag counters must be aligned for atomic access");
LONG64 MiProbeRead64(volatile LONG64 *Slot)
{
    return POOL_ATOMIC_READ64(Slot);
}
''')
        subprocess.run(command + ['-S', '-o', str(assembly), str(source)], cwd=entry['directory'], check=True)
        text = assembly.read_text()
        match = re.search(r'_?MiProbeRead64:.*?Lfunc_end\d+:', text, re.DOTALL)
        if not match or not re.search(r'\block\s+cmpxchg8b\b', match.group()):
            raise RuntimeError('i386 64-bit pool read is not an atomic compare-exchange')
    print('i386 memory-manager and pool 64-bit reads are indivisible')


if __name__ == '__main__':
    main()
