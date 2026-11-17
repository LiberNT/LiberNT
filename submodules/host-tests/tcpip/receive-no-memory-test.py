# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A TCP segment that cannot be queued for lack of memory is refused, not dereferenced through NULL
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--source', help='tcp.c to test instead of the one under --root')
args = parser.parse_args()
source = pathlib.Path(args.source or pathlib.Path(args.root) / 'drivers/network/tcpip/ip/lwip_glue/tcp.c').read_text()

enqueue = re.search(r'(?ms)^(?:void|BOOLEAN) LibTCPEnqueuePacket\(.*?^\}\n', source)
receive = re.search(r'(?ms)^static\nerr_t\nInternalRecvEventHandler\(.*?^\}\n', source)
assert enqueue and receive, 'the receive path was not found'

fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#define TRUE 1
#define FALSE 0
#define ERR_OK 0
#define ERR_MEM (-1)
#define ERR_CLSD (-15)
#define STATUS_SUCCESS 0
#define WFP_SHIM_STREAM_DISCONNECT 2
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); Failures++; } } while (0)
typedef signed char err_t;
typedef unsigned short u16_t;
typedef unsigned char BOOLEAN;
typedef void *PVOID;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
struct pbuf { u16_t tot_len; };
typedef struct { unsigned char urg_in_data; unsigned short urg_offset; } TCP_PCB, *PTCP_PCB;
typedef struct { struct pbuf *p; unsigned Offset; unsigned char Urgent; unsigned short UrgentOffset; LIST_ENTRY ListEntry; } QUEUE_ENTRY, *PQUEUE_ENTRY;
typedef struct
{
    LIST_ENTRY PacketQueue;
    unsigned WfpEndpointId;
    BOOLEAN ReceiveShutdown, SendShutdown;
    int ReceiveShutdownStatus;
    PVOID SocketContext;
} CONNECTION_ENDPOINT, *PCONNECTION_ENDPOINT;

static int Failures, FailAllocation, Freed, Indicated, Acknowledged;
static int QueueEntryLookasideList;

static PVOID ExAllocateFromNPagedLookasideList(int *List) { (void)List; return FailAllocation ? NULL : malloc(sizeof(QUEUE_ENTRY)); }
static void LockObject(PVOID Object) { (void)Object; }
static void UnlockObject(PVOID Object) { (void)Object; }
static void InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Flink = Head;
    Entry->Blink = Head->Blink;
    Head->Blink->Flink = Entry;
    Head->Blink = Entry;
}
static void LibTCPConfirmPath(PTCP_PCB pcb) { (void)pcb; }
static BOOLEAN LibTCPWfpReceive(PCONNECTION_ENDPOINT Connection, struct pbuf *p) { (void)Connection; (void)p; return TRUE; }
static void tcp_recved(PTCP_PCB pcb, u16_t len) { (void)pcb; (void)len; Acknowledged++; }
static void pbuf_free(struct pbuf *p) { (void)p; Freed++; }
static void TCPRecvEventHandler(PVOID arg) { (void)arg; Indicated++; }
static void TCPFinEventHandler(PVOID arg, err_t err) { (void)arg; (void)err; }
static void WfpShimClassifyStream(unsigned Id, int What, PVOID a, int b) { (void)Id; (void)What; (void)a; (void)b; }
static void tcp_arg(PTCP_PCB pcb, PVOID arg) { (void)pcb; (void)arg; }

''' + enqueue.group(0) + receive.group(0) + r'''
static sigjmp_buf Crashed;
static void OnFault(int Signal) { (void)Signal; siglongjmp(Crashed, 1); }

int main(void)
{
    CONNECTION_ENDPOINT Connection;
    TCP_PCB Pcb = { 0, 0 };
    struct pbuf Segment = { 100 };
    volatile int Result = 99;

    signal(SIGSEGV, OnFault);
    Connection.PacketQueue.Flink = Connection.PacketQueue.Blink = &Connection.PacketQueue;
    Connection.SendShutdown = Connection.ReceiveShutdown = FALSE;

    /* No memory for the queue entry: the segment is refused and still belongs to the stack */
    FailAllocation = 1;
    if (!sigsetjmp(Crashed, 1))
        Result = InternalRecvEventHandler(&Connection, &Pcb, &Segment, ERR_OK);
    else
        Result = -100;
    CHECK(Result == ERR_MEM);
    CHECK(Freed == 0 && Indicated == 0);
    CHECK(Connection.PacketQueue.Flink == &Connection.PacketQueue);

    /* With memory it is queued and indicated as before */
    FailAllocation = 0;
    CHECK(InternalRecvEventHandler(&Connection, &Pcb, &Segment, ERR_OK) == ERR_OK);
    CHECK(Indicated == 1 && Freed == 0);
    CHECK(Connection.PacketQueue.Flink != &Connection.PacketQueue);

    if (!Failures)
        printf("tcp receive: a segment is queued when there is memory for it and refused when there is not\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'tcp.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wno-unused-function', str(path / 'tcp.c'), '-o', str(path / 'tcp')], check=True)
    subprocess.run([str(path / 'tcp')], check=True)
