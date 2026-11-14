#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     x86 ReadAcquire/WriteRelease keep ordinary memory accesses on their side when optimized
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)

memaccess = (root / 'sdk/include/xdk/memaccess.h').read_text()
memaccess = re.sub(r'(?s)^\$if\(0\).*?\$endif\(\)\n', '', memaccess)
if '$' in memaccess:
    raise RuntimeError('unexpected template directive in memaccess.h')

intrin = (root / 'sdk/include/vcruntime/mingw32/intrin_x86.h').read_text()
barrier = re.search(r'(?s)__INTRIN_INLINE void _ReadWriteBarrier\(void\)\s*\{.*?\n\}', intrin)
if not barrier:
    raise RuntimeError('_ReadWriteBarrier not found in intrin_x86.h')

WIDTHS = [('8', 'char'), ('16', 'short'), ('', 'long'), ('64', '__int64')]

fixture = r'''
#define _VCRT_X86_INTRINSICS 1
#define _VCRT_AMD64_INTRINSICS 1
#define _VCRT_ARM64_INTRINSICS 0
#define _WIN64
#define __forceinline static inline __attribute__((always_inline))
#define __INTRIN_INLINE static inline __attribute__((always_inline))
#define _In_
#define _Out_
#define _Interlocked_operand_
#define __int64 long long
typedef unsigned char BOOLEAN;
''' + barrier.group(0) + '\n' + memaccess + r'''
#define HIDDEN __attribute__((visibility("hidden")))
HIDDEN int Data;
'''
for suffix, type_ in WIDTHS:
    name = suffix or '32'
    fixture += f'''
HIDDEN {type_} Flag{name};
int Acquire{name}(void)
{{
    int Before = Data;
    if (ReadAcquire{suffix}(&Flag{name}))
        return Data - Before;
    return -1;
}}
void Release{name}(void)
{{
    Data = 1;
    WriteRelease{suffix}(&Flag{name}, 1);
    Data = 2;
}}
'''


def body(asm, name):
    match = re.search(r'(?ms)^' + name + r':[^\n]*\n(.*?)^\s*\.size\s+' + name + r',', asm)
    if not match:
        raise RuntimeError('function not found in assembly: ' + name)
    return match.group(1)


def data_accesses(text):
    count = 0
    for line in text.splitlines():
        line = line.strip()
        if not re.search(r'\bData\b', line) or line.startswith('.'):
            continue
        if line.startswith('adrp') or '%hi(' in line or '%pcrel_hi(' in line:
            continue
        count += 1
    return count


with tempfile.TemporaryDirectory() as directory:
    source = pathlib.Path(directory) / 'memaccess.c'
    source.write_text(fixture)
    result = subprocess.run([args.cc, '-std=gnu11', '-O2', '-S', '-o', '-', '-fno-asynchronous-unwind-tables',
                             str(source)], capture_output=True, text=True)
    if result.returncode:
        print(result.stderr)
        sys.exit(1)
    asm = result.stdout

failures = 0
for suffix, _ in WIDTHS:
    name = suffix or '32'
    loads = data_accesses(body(asm, 'Acquire' + name))
    if loads != 2:
        print(f'ReadAcquire{suffix}: {loads} load(s) of Data, expected 2: a later load was satisfied before the acquire')
        failures += 1
    stores = data_accesses(body(asm, 'Release' + name))
    if stores != 2:
        print(f'WriteRelease{suffix}: {stores} store(s) to Data, expected 2: an earlier store was moved past the release')
        failures += 1

if failures:
    sys.exit(1)
print('memaccess order: 8 checks passed')
