# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Timer wait-block cleanup regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = pathlib.Path(args.source).read_text()


def function(name):
    start = source.index(name + '(')
    start = source.rfind('VOID\nFASTCALL\n', 0, start)
    end = source.index('{', start)
    depth = 1
    pos = end + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <pthread.h>
#include <stdint.h>
#define NTDDI_VERSION 0x06020000
#define NTDDI_WIN8 0x06020000
#define TIMER_WAIT_BLOCK 3
#define VOID void
#define FASTCALL
#define IN
#define FALSE 0
#define NULLPTR ((void *)0)
#define DBG_UNREFERENCED_LOCAL_VARIABLE(x) (void)(x)
#define ASSERT(x) assert(x)
typedef int BOOLEAN;
typedef unsigned long ULONG;
typedef intptr_t LONG_PTR;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct _HEADER { LIST_ENTRY WaitListHead; int Inserted; pthread_mutex_t Lock; } DISPATCHER_HEADER, *PDISPATCHER_HEADER;
typedef struct _TIMER { DISPATCHER_HEADER Header; } KTIMER, *PKTIMER;
typedef struct _KTHREAD KTHREAD, *PKTHREAD;
typedef struct _BLOCK { LIST_ENTRY WaitListEntry; void *Object; KTHREAD *Thread; int BlockState; } KWAIT_BLOCK, *PKWAIT_BLOCK;
typedef struct _PRCB { int dummy; } KPRCB, *PKPRCB;
struct _KTHREAD { LONG_PTR WaitStatus; int TimerActive; KTIMER Timer; KWAIT_BLOCK WaitBlock[4]; PKWAIT_BLOCK WaitBlockList; unsigned int WaitBlockCount; PKPRCB WaitPrcb; LIST_ENTRY WaitListEntry; void *Queue; };
enum { WaitBlockInactive, WaitBlockActive };
static int RemoveResult;
static int KxRemoveTreeTimer(PKTIMER Timer) { Timer->Header.Inserted = 0; return RemoveResult; }
static void InitializeListHead(PLIST_ENTRY e) { e->Flink=e; e->Blink=e; }
static void RemoveEntryList(PLIST_ENTRY e) { e->Blink->Flink=e->Flink; e->Flink->Blink=e->Blink; }
static void InsertTailList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Blink=h->Blink; e->Flink=h; h->Blink->Flink=e; h->Blink=e; }
static void KiAssertWaitListEntry(PLIST_ENTRY e) { assert(e->Flink && e->Blink && e->Flink->Blink==e && e->Blink->Flink==e); }
static PDISPATCHER_HEADER TrackedTimer;
static int EnteredTimer, Finished;
static void KiAcquireDispatcherObject(PDISPATCHER_HEADER h) { if (h==TrackedTimer) __atomic_store_n(&EnteredTimer, 1, __ATOMIC_RELEASE); assert(pthread_mutex_lock(&h->Lock)==0); }
static void KiReleaseDispatcherObject(PDISPATCHER_HEADER h) { assert(pthread_mutex_unlock(&h->Lock)==0); }
static void KiAcquireWaitLock(PKPRCB p) { (void)p; }
static void KiReleaseWaitLock(PKPRCB p) { (void)p; }
static void KiIncrementQueueCurrentCount(void *p) { (void)p; }
'''
fixture += '\n' + function('KiUnlinkThread') + '\n' + function('KiUnlinkWaitBlocks')
fixture += r'''
static void run(int removed)
{
    KTHREAD t = {0};
    DISPATCHER_HEADER object = {0};
    KWAIT_BLOCK *ordinary = &t.WaitBlock[0], *timer = &t.WaitBlock[3];
    assert(pthread_mutex_init(&object.Lock, 0)==0);
    assert(pthread_mutex_init(&t.Timer.Header.Lock, 0)==0);
    InitializeListHead(&object.WaitListHead);
    InitializeListHead(&t.Timer.Header.WaitListHead);
    ordinary->Object=&object;
    ordinary->Thread=&t;
    ordinary->BlockState=WaitBlockActive;
    timer->Thread=&t;
    timer->BlockState=WaitBlockActive;
    InsertTailList(&object.WaitListHead, &ordinary->WaitListEntry);
    InsertTailList(&t.Timer.Header.WaitListHead, &timer->WaitListEntry);
    t.WaitBlockList=ordinary;
    t.WaitBlockCount=1;
    t.TimerActive=1;
    t.Timer.Header.Inserted=1;
    RemoveResult=removed;
    KiUnlinkThread(&t, 0);
    KiUnlinkWaitBlocks(&t);
    assert(object.WaitListHead.Flink==&object.WaitListHead);
    assert(t.Timer.Header.WaitListHead.Flink==&t.Timer.Header.WaitListHead);
    assert(t.Timer.Header.WaitListHead.Blink==&t.Timer.Header.WaitListHead);
    assert(timer->WaitListEntry.Flink==NULLPTR);
    assert(timer->BlockState==WaitBlockInactive);
    assert(!t.TimerActive);
    pthread_mutex_destroy(&object.Lock);
    pthread_mutex_destroy(&t.Timer.Header.Lock);
}
static void *cleanup(void *arg)
{
    KiUnlinkWaitBlocks(arg);
    __atomic_store_n(&Finished, 1, __ATOMIC_RELEASE);
    return NULL;
}
static void test_timer_lock(void)
{
    KTHREAD t = {0};
    DISPATCHER_HEADER object = {0};
    KWAIT_BLOCK *ordinary = &t.WaitBlock[0], *timer = &t.WaitBlock[3];
    pthread_t thread;
    assert(pthread_mutex_init(&object.Lock, 0)==0);
    assert(pthread_mutex_init(&t.Timer.Header.Lock, 0)==0);
    InitializeListHead(&object.WaitListHead);
    InitializeListHead(&t.Timer.Header.WaitListHead);
    ordinary->Object=&object;
    ordinary->Thread=&t;
    ordinary->BlockState=WaitBlockActive;
    timer->Thread=&t;
    timer->BlockState=WaitBlockActive;
    InsertTailList(&object.WaitListHead, &ordinary->WaitListEntry);
    InsertTailList(&t.Timer.Header.WaitListHead, &timer->WaitListEntry);
    t.WaitBlockList=ordinary;
    t.WaitBlockCount=1;
    t.TimerActive=1;
    t.Timer.Header.Inserted=1;
    RemoveResult=0;
    KiUnlinkThread(&t, 0);
    TrackedTimer=&t.Timer.Header;
    assert(pthread_mutex_lock(&t.Timer.Header.Lock)==0);
    assert(pthread_create(&thread, 0, cleanup, &t)==0);
    while (!__atomic_load_n(&EnteredTimer, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&Finished, __ATOMIC_ACQUIRE)) { }
    assert(__atomic_load_n(&EnteredTimer, __ATOMIC_ACQUIRE));
    assert(!__atomic_load_n(&Finished, __ATOMIC_ACQUIRE));
    assert(pthread_mutex_unlock(&t.Timer.Header.Lock)==0);
    assert(pthread_join(thread, 0)==0);
    assert(t.Timer.Header.WaitListHead.Flink==&t.Timer.Header.WaitListHead);
    assert(timer->WaitListEntry.Flink==NULLPTR);
    TrackedTimer=NULLPTR;
    pthread_mutex_destroy(&object.Lock);
    pthread_mutex_destroy(&t.Timer.Header.Lock);
}
static void test_timer_only(int active)
{
    KTHREAD t = {0};
    KWAIT_BLOCK *timer = &t.WaitBlock[3];
    assert(pthread_mutex_init(&t.Timer.Header.Lock, 0)==0);
    InitializeListHead(&t.Timer.Header.WaitListHead);
    timer->Thread=&t;
    timer->BlockState=active ? WaitBlockActive : WaitBlockInactive;
    if (active) InsertTailList(&t.Timer.Header.WaitListHead, &timer->WaitListEntry);
    t.WaitBlockList=timer;
    t.WaitBlockCount=1;
    t.Timer.Header.Inserted=active;
    RemoveResult=1;
    KiUnlinkThread(&t, 0);
    KiUnlinkWaitBlocks(&t);
    assert(t.Timer.Header.WaitListHead.Flink==&t.Timer.Header.WaitListHead);
    assert(t.Timer.Header.WaitListHead.Blink==&t.Timer.Header.WaitListHead);
    assert(timer->WaitListEntry.Flink==NULLPTR);
    assert(timer->BlockState==WaitBlockInactive);
    pthread_mutex_destroy(&t.Timer.Header.Lock);
}
int main(void) { run(0); run(1); test_timer_lock(); test_timer_only(0); test_timer_only(1); return 0; }
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'wait_timer.c'
    exe = pathlib.Path(tmp) / 'wait_timer'
    c.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)