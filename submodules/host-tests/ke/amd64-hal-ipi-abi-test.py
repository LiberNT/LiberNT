# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Check the AMD64 HAL's legacy mask calls at the kernel/HAL boundary
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)
source = (root / 'hal/halx86/smp/ipi.c').read_text()
wrappers = source[source.index('/* GLOBALS'):]

fixture = r'''
#include <assert.h>
#include <stdint.h>
#define _In_
#define _M_AMD64
#define NTAPI
#define VOID void
typedef unsigned int ULONG;
typedef uintptr_t KAFFINITY;
typedef unsigned char KIRQL;
void HalRequestIpi(KAFFINITY);
void HalSendNMI(KAFFINITY);
void HalSendSoftwareInterrupt(KAFFINITY, KIRQL);
static const KAFFINITY Mask = (KAFFINITY)1 << 47;
static unsigned int Calls;
static void HalpRequestIpi(ULONG w, KAFFINITY m) { assert(w == 0 && m == Mask); Calls++; }
static void HalpSendNMI(ULONG w, KAFFINITY m) { assert(w == 0 && m == Mask); Calls++; }
static void HalpSendSoftwareInterrupt(ULONG w, KAFFINITY m, KIRQL i) { assert(w == 0 && m == Mask && i == 2); Calls++; }
''' + wrappers + r'''
int main(void) {
    HalRequestIpi(Mask);
    HalSendNMI(Mask);
    HalSendSoftwareInterrupt(Mask, 2);
    assert(Calls == 3);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    test = pathlib.Path(directory) / 'hal-ipi'
    test.with_suffix('.c').write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-Werror', str(test.with_suffix('.c')), '-o', str(test)], check=True)
    subprocess.run([str(test)], check=True)
