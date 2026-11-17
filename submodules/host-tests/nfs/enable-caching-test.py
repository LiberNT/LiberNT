# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The NFS redirector gives the open-list mutex back when it cannot allocate a list entry
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--source', help='nfs41_driver.c to test instead of the one under --root')
args = parser.parse_args()
source = pathlib.Path(args.source or pathlib.Path(args.root) / 'drivers/filesystems/nfs/nfs41_driver.c').read_text(errors='replace')

match = re.search(r'(?ms)^void enable_caching\(.*?^\}\n', source)
assert match, 'enable_caching not found'

fixture = r'''
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TRUE 1
#define FALSE 0
#define FILE_READ_DATA 1
#define FILE_WRITE_DATA 2
#define ENABLE_READ_CACHING 1
#define ENABLE_WRITE_CACHING 2
#define ENABLE_READWRITE_CACHING 3
#define NonPagedPool 0
#define NFS41_MM_POOLTAG_OPEN 0
#define ULongToPtr(x) ((PVOID)(size_t)(x))
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); Failures++; } } while (0)
typedef unsigned char BOOLEAN;
typedef unsigned int ULONG;
typedef unsigned long long ULONGLONG;
typedef void *PVOID, *HANDLE;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct { int Owned; } FAST_MUTEX;
typedef struct { ULONG DesiredAccess; PVOID pFcb; PVOID pAlreadyPrefixedName; } MRX_SRV_OPEN, *PMRX_SRV_OPEN, *PSRV_OPEN;
typedef struct { BOOLEAN write_thru; ULONG deleg_type; } NFS41_FOBX, *PNFS41_FOBX;
typedef struct
{
    LIST_ENTRY next;
    PVOID fcb;
    HANDLE session;
    PNFS41_FOBX nfs41_fobx;
    ULONGLONG ChangeTime;
    BOOLEAN skip;
} nfs41_fcb_list_entry;

static int Failures, FailAllocation;
static FAST_MUTEX fcblistLock;
static struct { LIST_ENTRY head; } openlist;

static void ExAcquireFastMutex(FAST_MUTEX *Mutex) { CHECK(!Mutex->Owned); Mutex->Owned = 1; }
static void ExReleaseFastMutex(FAST_MUTEX *Mutex) { CHECK(Mutex->Owned); Mutex->Owned = 0; }
static BOOLEAN IsListEmpty(PLIST_ENTRY Head) { return Head->Flink == Head; }
static void InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Flink = Head;
    Entry->Blink = Head->Blink;
    Head->Blink->Flink = Entry;
    Head->Blink = Entry;
}
static void RxChangeBufferingState(PSRV_OPEN SrvOpen, PVOID Context, BOOLEAN Compute) { (void)SrvOpen; (void)Context; (void)Compute; }
static PVOID RxAllocatePoolWithTag(int Type, size_t Size, ULONG Tag) { (void)Type; (void)Tag; return FailAllocation ? NULL : calloc(1, Size); }

''' + match.group(0) + r'''
int main(void)
{
    MRX_SRV_OPEN SrvOpen = { FILE_READ_DATA, (PVOID)(size_t)0x1000, NULL };
    NFS41_FOBX Fobx = { FALSE, 1 };

    openlist.head.Flink = openlist.head.Blink = &openlist.head;

    /* A recalled delegation needs a list entry, and there is no memory for one */
    FailAllocation = 1;
    enable_caching(&SrvOpen, &Fobx, 1, NULL);
    CHECK(!fcblistLock.Owned);
    CHECK(IsListEmpty(&openlist.head));

    /* The next caller can still take the mutex, and with memory the entry is listed */
    FailAllocation = 0;
    enable_caching(&SrvOpen, &Fobx, 1, NULL);
    CHECK(!fcblistLock.Owned);
    CHECK(!IsListEmpty(&openlist.head));
    CHECK(Fobx.deleg_type == 0);

    if (!Failures)
        printf("nfs enable caching: the open-list mutex is released whether or not the entry could be allocated\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'nfs.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wno-unused-function', '-Wno-unused-variable',
                    str(path / 'nfs.c'), '-o', str(path / 'nfs')], check=True)
    subprocess.run([str(path / 'nfs')], check=True)
