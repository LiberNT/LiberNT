# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     AMD64 TLB message handling across nested freeze NMIs
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/mm/arch/amd64/tlb.c').read_text()
start = source.index('MiAmd64ProcessTlbRequest(')
start = source.index('{', start)
depth = 1
end = start + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[start + 1:end - 1]
body = body.replace(
    'MiAmd64FlushLocal(Message->Address, Message->Pages)',
    'MiAmd64FlushLocal(TestReadPayload(&Message->Address), TestReadPayload(&Message->Pages))')

fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint32_t ULONG;
typedef uint64_t ULONG64;
typedef unsigned char BOOLEAN;
#define VOID void
#define TRUE 1
#define FALSE 0
#define MAXIMUM_PROCESSORS 2
#define MI_ASSERT(e) assert(e)
typedef struct { ULONG64 Address, Pages, Published, Completed; } MI_AMD64_TLB_MESSAGE;
static MI_AMD64_TLB_MESSAGE MiAmd64TlbMessages[2][2];
static int Mode, InHook, Injected, Flushes, Mixed;
static void MiAmd64ProcessTlbRequest(ULONG Source);
static MI_AMD64_TLB_MESSAGE *Slot(void) { return &MiAmd64TlbMessages[0][1]; }
static BOOLEAN MiAmd64TlbDisableInterrupts(void) { return FALSE; }
static void MiAmd64TlbRestoreInterrupts(BOOLEAN enabled) { assert(!enabled); }
static ULONG MiAmd64TlbCpu(void) { return 0; }
static void Interleave(void) {
    MI_AMD64_TLB_MESSAGE *message = Slot();
    assert(!InHook && !Injected);
    InHook = 1;
    Injected = 1;
    MiAmd64ProcessTlbRequest(1);
    assert(__atomic_load_n(&message->Completed, __ATOMIC_ACQUIRE) == 1);
    __atomic_store_n(&message->Address, 0x8000, __ATOMIC_RELAXED);
    __atomic_store_n(&message->Pages, 2, __ATOMIC_RELAXED);
    __atomic_store_n(&message->Published, 2, __ATOMIC_RELEASE);
    if (Mode != 3) {
        MiAmd64ProcessTlbRequest(1);
        assert(__atomic_load_n(&message->Completed, __ATOMIC_ACQUIRE) == 2);
    }
    InHook = 0;
}
static ULONG64 TestReadPayload(ULONG64 *p) {
    ULONG64 value = __atomic_load_n(p, __ATOMIC_RELAXED);
    if ((Mode == 2 || Mode == 3) && !InHook && !Injected) Interleave();
    return value;
}
static ULONG64 TestRead64(ULONG64 *p, int order) {
    if (p == &Slot()->Address || p == &Slot()->Pages) return TestReadPayload(p);
    ULONG64 value = __atomic_load_n(p, order);
    if (!InHook && !Injected &&
        ((Mode == 4 && p == &Slot()->Completed) ||
         (Mode == 5 && p == &Slot()->Published))) Interleave();
    return value;
}
#define MI_AMD64_READ_ACQUIRE64(p) TestRead64((p), __ATOMIC_ACQUIRE)
#define MI_AMD64_READ_NOFENCE64(p) TestRead64((p), __ATOMIC_RELAXED)
#define MI_AMD64_WRITE_RELEASE64(p,v) __atomic_store_n((p),(v), __ATOMIC_RELEASE)
#define MI_AMD64_COMPARE_EXCHANGE64(p,expected,value) __atomic_compare_exchange_n((p), &(expected), (value), FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)
static void MiAmd64FlushLocal(ULONG64 address, ULONG64 pages) {
    if (Mode == 1 && !InHook && !Injected) Interleave();
    Flushes++;
    if (!((address == 0x1000 && pages == 1) || (address == 0x8000 && pages == 2))) Mixed++;
}
static void MiAmd64ProcessTlbRequest(ULONG Source) { @BODY@ }
int main(int argc, char **argv) {
    MI_AMD64_TLB_MESSAGE *message = Slot();
    assert(argc == 2);
    Mode = atoi(argv[1]);
    message->Address = 0x1000;
    message->Pages = 1;
    message->Published = 1;
    MiAmd64ProcessTlbRequest(1);
    if (Mode == 3) {
        if (message->Completed != 1 || Mixed != 0) {
            fprintf(stderr, "premature acknowledgement or mixed flush: completed=%llu mixed=%d\n",
                    (unsigned long long)message->Completed, Mixed);
            return 1;
        }
        MiAmd64ProcessTlbRequest(1);
    }
    if (!Injected || message->Completed != 2 || message->Published != 2 || Mixed != 0) {
        fprintf(stderr, "mode=%d injected=%d published=%llu completed=%llu flushes=%d mixed=%d\n",
                Mode, Injected, (unsigned long long)message->Published,
                (unsigned long long)message->Completed, Flushes, Mixed);
        return 1;
    }
    return 0;
}
'''.replace('@BODY@', body)

with tempfile.TemporaryDirectory(prefix='mm-amd64-tlb-nmi-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    results = [subprocess.run([str(path / 'test'), str(mode)], timeout=5).returncode
               for mode in (1, 2, 3, 4, 5)]
    if any(results):
        raise SystemExit(1)
