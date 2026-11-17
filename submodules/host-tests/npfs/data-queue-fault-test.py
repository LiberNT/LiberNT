# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A pipe write whose buffer faults while it is copied frees the queue entry it had allocated
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--source', help='datasup.c to test instead of the one under --root')
args = parser.parse_args()
source = pathlib.Path(args.source or pathlib.Path(args.root) / 'drivers/filesystems/npfs/datasup.c').read_text(errors='replace')

match = re.search(r'(?ms)^NTSTATUS\nNTAPI\nNpAddDataQueueEntry\(.*?^\}\n', source)
assert match, 'NpAddDataQueueEntry not found'

fixture = r'''
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NTAPI
#define IN
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_PENDING 0x103
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xC0000005)
#define NT_SUCCESS(s) ((s) >= 0)
#define NonPagedPool 0
#define POOL_QUOTA_FAIL_INSTEAD_OF_RAISE 8
#define NPFS_DATA_ENTRY_TAG 0
#define EXCEPTION_EXECUTE_HANDLER 1
#define ASSERT(x) ((void)0)
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); Failures++; } } while (0)
/* The guarded block, with a fault in it delivered to the handler */
static sigjmp_buf Faulted;
#define _SEH2_TRY if (!sigsetjmp(Faulted, 1))
#define _SEH2_EXCEPT(x) else
#define _SEH2_END
#define _SEH2_YIELD(x) x
#define _SEH2_GetExceptionCode() STATUS_ACCESS_VIOLATION
typedef int NTSTATUS;
typedef unsigned char BOOLEAN;
typedef unsigned int ULONG;
typedef size_t SIZE_T;
typedef void *PVOID, *PNP_CCB, *PSECURITY_CLIENT_CONTEXT;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
enum { Empty, ReadEntries, WriteEntries };
enum { Buffered, Unbuffered };
struct _IRP;
typedef void (*PDRIVER_CANCEL)(PVOID, struct _IRP *);
typedef struct _IRP
{
    PVOID UserBuffer;
    BOOLEAN Cancel;
    PDRIVER_CANCEL CancelRoutine;
    struct { struct { PVOID Thread; PVOID DriverContext[4]; } Overlay; } Tail;
} IRP, *PIRP;
typedef struct _NP_DATA_QUEUE
{
    LIST_ENTRY Queue;
    ULONG QueueState, BytesInQueue, EntriesInQueue, QuotaUsed, ByteOffset, Quota;
} NP_DATA_QUEUE, *PNP_DATA_QUEUE;
typedef struct _NP_DATA_QUEUE_ENTRY
{
    LIST_ENTRY QueueEntry;
    ULONG DataEntryType;
    PIRP Irp;
    ULONG QuotaInEntry;
    PSECURITY_CLIENT_CONTEXT ClientSecurityContext;
    ULONG DataSize;
} NP_DATA_QUEUE_ENTRY, *PNP_DATA_QUEUE_ENTRY;

static int Failures, FaultTheCopy, Allocated, Freed, ContextsFreed;

static PVOID PsGetCurrentThread(void) { return NULL; }
static NTSTATUS NpGetClientSecurityContext(ULONG End, PNP_CCB Ccb, PVOID Thread, PSECURITY_CLIENT_CONTEXT *Context)
{
    (void)End; (void)Ccb; (void)Thread;
    *Context = (PVOID)(size_t)0x10;
    return STATUS_SUCCESS;
}
static void NpFreeClientSecurityContext(PSECURITY_CLIENT_CONTEXT Context) { if (Context) ContextsFreed++; }
static PVOID ExAllocatePoolWithQuotaTag(int Type, SIZE_T Size, ULONG Tag) { (void)Type; (void)Tag; Allocated++; return calloc(1, Size); }
static void ExFreePoolWithTag(PVOID Block, ULONG Tag) { (void)Tag; Freed++; free(Block); }
static void RtlCopyMemory(PVOID To, const void *From, SIZE_T Size)
{
    /* The client unmapped its buffer after the write was accepted */
    if (FaultTheCopy)
        siglongjmp(Faulted, 1);
    memcpy(To, From, Size);
}
static void InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Flink = Head;
    Entry->Blink = Head->Blink;
    Head->Blink->Flink = Entry;
    Head->Blink = Entry;
}
static BOOLEAN IsListEmpty(PLIST_ENTRY Head) { return Head->Flink == Head; }
static void IoMarkIrpPending(PIRP Irp) { (void)Irp; }
static PDRIVER_CANCEL IoSetCancelRoutine(PIRP Irp, PDRIVER_CANCEL Routine) { PDRIVER_CANCEL Old = Irp->CancelRoutine; Irp->CancelRoutine = Routine; return Old; }
static void NpCancelDataQueueIrp(PVOID DeviceObject, PIRP Irp) { (void)DeviceObject; (void)Irp; }

''' + match.group(0) + r'''
int main(void)
{
    NP_DATA_QUEUE Queue;
    char Data[32] = "payload";
    IRP Irp;

    memset(&Queue, 0, sizeof(Queue));
    Queue.Queue.Flink = Queue.Queue.Blink = &Queue.Queue;
    Queue.Quota = 4096;
    memset(&Irp, 0, sizeof(Irp));
    Irp.UserBuffer = Data;

    /* The copy of the client's data faults: the write fails and nothing it allocated is left behind */
    FaultTheCopy = 1;
    CHECK(NpAddDataQueueEntry(0, NULL, &Queue, WriteEntries, Buffered, sizeof(Data), &Irp, NULL, 0) == STATUS_ACCESS_VIOLATION);
    CHECK(Allocated == 1);
    CHECK(Freed == 1);
    CHECK(ContextsFreed == 1);
    CHECK(IsListEmpty(&Queue.Queue));
    CHECK(Queue.EntriesInQueue == 0 && Queue.BytesInQueue == 0);

    /* A write whose buffer holds is queued, and its entry stays with the queue */
    FaultTheCopy = 0;
    CHECK(NT_SUCCESS(NpAddDataQueueEntry(0, NULL, &Queue, WriteEntries, Buffered, sizeof(Data), &Irp, NULL, 0)));
    CHECK(Allocated == 2 && Freed == 1);
    CHECK(Queue.EntriesInQueue == 1 && Queue.BytesInQueue == sizeof(Data));

    if (!Failures)
        printf("npfs data queue: an entry whose data could not be copied is freed, one that was copied is queued\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'datasup.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-clobbered',
                    str(path / 'datasup.c'), '-o', str(path / 'datasup')], check=True)
    subprocess.run([str(path / 'datasup')], check=True)
