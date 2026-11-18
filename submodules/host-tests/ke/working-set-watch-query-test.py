# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later
# PURPOSE:     Exercise the working-set watch while a user copy is outstanding
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True)
parser.add_argument('--root', required=True, type=pathlib.Path)
args = parser.parse_args()
source = (args.root / 'ntos/ps/psaccessors.c').read_text()
fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#define VOID void
#define NTAPI
#define _In_
#define _Out_
#define _Out_writes_bytes_(x)
#define PAGED_CODE()
#define ASSERT(x) assert(x)
#define NT_SUCCESS(x) ((x) >= 0)
#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL -1
#define STATUS_NO_MORE_ENTRIES -2
#define STATUS_BUFFER_TOO_SMALL -3
#define PSP_WS_WATCH_RECORDS 4
#define TRUE 1
#define FALSE 0
typedef void *PVOID, *HANDLE;
typedef int NTSTATUS, KSPIN_LOCK, KIRQL;
typedef unsigned char BOOLEAN;
typedef uintptr_t ULONG_PTR;
typedef unsigned int ULONG, *PULONG;
typedef struct { PVOID FaultingPc, FaultingVa; } PROCESS_WS_WATCH_INFORMATION;
typedef struct { PVOID WorkingSetWatch; } EPROCESS, *PEPROCESS;
static HANDLE PsGetCurrentThreadId(void) { return (HANDLE)1; }
static void KeAcquireSpinLock(KSPIN_LOCK *lock, KIRQL *old) { assert(!*lock); *lock = 1; *old = 0; }
static void KeReleaseSpinLock(KSPIN_LOCK *lock, KIRQL old) { assert(*lock && !old); *lock = 0; }
'''
fixture += source[source.index('typedef struct _PSP_WS_WATCH_RECORD'):source.index('static\nNTSTATUS\nPspCopyWorkingSetWatch')]
fixture += '''
static EPROCESS process;
static NTSTATUS copyStatus;
static NTSTATUS PspCopyWorkingSetWatch(PVOID, ULONG, const PSP_WS_WATCH_RECORD *, ULONG, ULONG, BOOLEAN);
'''
fixture += re.search(r'^NTSTATUS\nNTAPI\nPspQueryWorkingSetWatch\(.*?^}', source, re.M | re.S)[0]
fixture += '''
static NTSTATUS PspCopyWorkingSetWatch(PVOID buffer, ULONG length, const PSP_WS_WATCH_RECORD *records,
                                       ULONG count, ULONG lost, BOOLEAN extended)
{
    ULONG size = 99;
    (void)buffer;
    assert(!extended && length >= 2 * sizeof(PROCESS_WS_WATCH_INFORMATION));
    assert(count == 1 && !lost && records[0].FaultingVa == (PVOID)2);
    assert(PspQueryWorkingSetWatch(&process, NULL, length, &size, TRUE) == STATUS_NO_MORE_ENTRIES);
    assert(size == 0);
    PsWatchWorkingSet(&process, (PVOID)3, (PVOID)4);
    return copyStatus;
}
int main(void)
{
    PSP_WS_WATCH watch = {0};
    ULONG size;
    process.WorkingSetWatch = &watch;
    PsWatchWorkingSet(&process, (PVOID)1, (PVOID)2);
    assert(PspQueryWorkingSetWatch(&process, NULL, 0, &size, FALSE) == STATUS_BUFFER_TOO_SMALL);
    assert(size == 2 * sizeof(PROCESS_WS_WATCH_INFORMATION));
    copyStatus = STATUS_UNSUCCESSFUL;
    assert(PspQueryWorkingSetWatch(&process, NULL, size, &size, FALSE) == STATUS_UNSUCCESSFUL);
    assert(watch.Count == 1 && !watch.Lost);
    copyStatus = STATUS_SUCCESS;
    assert(PspQueryWorkingSetWatch(&process, NULL, size, &size, FALSE) == STATUS_SUCCESS);
    assert(!watch.Count && !watch.Lost);
    PsWatchWorkingSet(&process, (PVOID)3, (PVOID)4);
    assert(watch.Count == 1 && watch.Records[0].FaultingVa == (PVOID)4);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    program = pathlib.Path(directory) / 'ws-watch'
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-x', 'c', '-', '-o', program],
                   input=fixture, text=True, check=True)
    subprocess.run([program], check=True)
