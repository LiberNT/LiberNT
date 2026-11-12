# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Releasing a TLS index clears the cell the thread's own code uses, never an emulation layer's
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/ps/psinfothreadset.c').read_text()


def definition(name):
    index = source.index('\n' + name + '(')
    start = index
    while True:
        line_start = source.rfind('\n', 0, start) + 1
        line = source[line_start:start].strip()
        if line == '' or line.endswith('*/') or line.endswith(';') or line.endswith('}') or line.startswith('#'):
            break
        start = line_start - 1
    start = start + 1 if source[start] == '\n' else start
    depth = 1
    end = source.index('{', index) + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


fixture = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VOID void
#define _In_
#define TRUE 1
#define FALSE 0
#define TLS_MINIMUM_AVAILABLE 64
#define TLS_EXPANSION_SLOTS 1024
#define STATUS_SUCCESS ((NTSTATUS)0)
#define PAGED_CODE()
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
#define _SEH2_TRY
#define _SEH2_EXCEPT(x) if (0)
#define _SEH2_END
#define _SEH2_GetExceptionCode() 0
#define UlongToPtr(u) ((PVOID)(uintptr_t)(u))
typedef unsigned char BOOLEAN, UCHAR, *PUCHAR;
typedef uint32_t ULONG, *PULONG;
typedef int32_t LONG, NTSTATUS;
typedef void *PVOID;
typedef struct { PVOID TlsSlots[64]; PVOID *TlsExpansionSlots; LONG WowTebOffset; } TEB, *PTEB;
typedef struct { ULONG TlsSlots[64]; ULONG TlsExpansionSlots; } TEB32;
static const char *Scenario = "setup";
static void ProbeForWrite(PVOID Address, size_t Length, ULONG Alignment) {
    CHECK(Address != NULL && Length != 0 && ((uintptr_t)Address % Alignment) == 0);
}
'''
fixture += definition('PspZeroTebTlsCell')
fixture += r'''
static struct { TEB Teb; char Gap[4096]; TEB32 Teb32; } Block;
#define NATIVE ((PVOID)(uintptr_t)0x1111)
static void Reset(LONG WowTebOffset) {
    memset(&Block, 0, sizeof(Block));
    Block.Teb.TlsSlots[10] = NATIVE;
    Block.Teb32.TlsSlots[10] = 0x2222;
    Block.Teb.WowTebOffset = WowTebOffset;
}
int main(void) {
    PVOID Expansion[TLS_EXPANSION_SLOTS] = { 0 };

    Scenario = "native thread";
    Reset(0);
    CHECK(PspZeroTebTlsCell(&Block.Teb, FALSE, 10) == STATUS_SUCCESS);
    CHECK(Block.Teb.TlsSlots[10] == NULL);

    Scenario = "native thread, expansion index";
    Reset(0);
    Block.Teb.TlsExpansionSlots = Expansion;
    Expansion[3] = NATIVE;
    CHECK(PspZeroTebTlsCell(&Block.Teb, FALSE, TLS_MINIMUM_AVAILABLE + 3) == STATUS_SUCCESS);
    CHECK(Expansion[3] == NULL);
    CHECK(Block.Teb.TlsSlots[3] == NULL && Block.Teb.TlsSlots[10] == NATIVE);

    Scenario = "emulated thread";
    Reset((LONG)offsetof(__typeof__(Block), Teb32));
    CHECK(PspZeroTebTlsCell(&Block.Teb, TRUE, 10) == STATUS_SUCCESS);
    CHECK(Block.Teb32.TlsSlots[10] == 0);
    CHECK(Block.Teb.TlsSlots[10] == NATIVE);

    /* The process cleared its own offset: the x86 block cannot be found, and the native cell is not its code's */
    Scenario = "emulated thread without an x86 block";
    Reset(0);
    CHECK(PspZeroTebTlsCell(&Block.Teb, TRUE, 10) == STATUS_SUCCESS);
    CHECK(Block.Teb.TlsSlots[10] == NATIVE);
    CHECK(Block.Teb32.TlsSlots[10] == 0x2222);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='ps-tls-cell-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
