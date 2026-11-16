# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later
# PURPOSE:     Verify processor indices against group-relative affinity masks
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True, type=pathlib.Path)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (args.root / 'ntos/ke/procgrp.c').read_text()
fixture = '''
#include <assert.h>
#include <stdint.h>
#define NTAPI
#define VOID void
#define _In_
#define _Inout_
#define FALSE 0
typedef uint16_t USHORT;
typedef uint32_t ULONG, LOGICAL;
typedef uint64_t KAFFINITY;
typedef struct { USHORT Count, Size; KAFFINITY Bitmap[20]; } KAFFINITY_EX, *PKAFFINITY_EX;
static ULONG KiMaximumGroupSize;
'''
for name in ('KeAddGroupAffinityEx', 'KeRemoveGroupAffinityEx', 'KeAddProcessorAffinityEx',
             'KeRemoveProcessorAffinityEx', 'KeCheckProcessorAffinityEx'):
    fixture += re.search(r'^(?:VOID|LOGICAL)\nNTAPI\n' + name + r'\(.*?^}', source, re.M | re.S)[0] + '\n'

fixture += '''
int main(void)
{
    for (KiMaximumGroupSize = 1; KiMaximumGroupSize <= 64; KiMaximumGroupSize *= 2)
    {
        KAFFINITY_EX affinity = { .Count = 1, .Size = 20 };
        ULONG index = 19 * KiMaximumGroupSize + KiMaximumGroupSize - 1;
        KAFFINITY mask = (KAFFINITY)1 << (KiMaximumGroupSize - 1);
        KeAddProcessorAffinityEx(&affinity, index);
        assert(affinity.Count == 20 && affinity.Bitmap[19] == mask);
        assert(KeCheckProcessorAffinityEx(&affinity, index));
        assert(!KeCheckProcessorAffinityEx(&affinity, index - KiMaximumGroupSize));
        KeRemoveProcessorAffinityEx(&affinity, index);
        assert(!KeCheckProcessorAffinityEx(&affinity, index) && !affinity.Bitmap[19]);
        KeAddProcessorAffinityEx(&affinity, 65536 * KiMaximumGroupSize);
        assert(!affinity.Bitmap[0]);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    program = pathlib.Path(directory) / 'affinity'
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-x', 'c', '-o', program, '-'],
                   input=fixture, text=True, check=True)
    subprocess.run([program], check=True)
