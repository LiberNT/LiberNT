# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Keyboard HID repeat cannot rearm after read shutdown
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'drivers/hid/kbdhid/kbdhid.c').read_text(encoding='latin-1')
functions = []
for name in ('KbdHid_RepeatDpc', 'KbdHid_UpdateRepeat', 'KbdHid_StopRead', 'KbdHid_EnableRepeat', 'KbdHid_MarkStarted'):
    match = re.search(r'(?ms)^static\nVOID\n(?:NTAPI\n)?' + name + r'\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found')
    functions.append(match.group(0))
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define IN
#define NTAPI
#define VOID void
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define ASSERT assert
#define DISPATCH_LEVEL 2
#define HIDP_STATUS_SUCCESS 0
#define HidP_Keyboard_Make 1
#define TRUE 1
#define FALSE 0
#define HID_USAGE_PAGE_KEYBOARD 7
#define Executive 0
#define KernelMode 0
#define NULL ((void *)0)
typedef void *PVOID;
typedef struct { int unused; } KDPC, *PKDPC;
typedef unsigned int ULONG;
typedef unsigned char BOOLEAN, KIRQL;
typedef struct { unsigned short Usage, UsagePage; } USAGE_AND_PAGE, *PUSAGE_AND_PAGE;
typedef struct {
    int InputLock, RepeatTimer;
    USAGE_AND_PAGE RepeatUsage, MakeUsageList[2], BreakUsageList[2];
    ULONG UsageListLength;
    int ModifierState;
    BOOLEAN RepeatEnabled, RepeatWanted, DeviceStarted, ReadReportActive, StopReadReport;
    void *Irp;
    int ReadCompletionEvent;
} KBDHID_DEVICE_EXTENSION, *PKBDHID_DEVICE_EXTENSION;
static int Locked, Arms, CancelCalls, FlushCalls, InjectCompletion, Translations;
static KBDHID_DEVICE_EXTENSION *InjectedDevice;
static void KeAcquireSpinLock(int *lock, KIRQL *old) { (void)lock; assert(!Locked); Locked=1; *old=0; }
static void KeReleaseSpinLock(int *lock, KIRQL old) { (void)lock; (void)old; assert(Locked); Locked=0; }
static KIRQL KeGetCurrentIrql(void) { return DISPATCH_LEVEL; }
static void KeAcquireSpinLockAtDpcLevel(int *lock) { KIRQL old; KeAcquireSpinLock(lock, &old); }
static void KeReleaseSpinLockFromDpcLevel(int *lock) { KeReleaseSpinLock(lock, 0); }
static int KbdHid_InsertScanCodes(void *ctx, char *codes, ULONG count) { (void)ctx; (void)codes; (void)count; return TRUE; }
static int HidP_TranslateUsageAndPagesToI8042ScanCodes(PUSAGE_AND_PAGE usage, ULONG count, int make, int *state, int (*insert)(void *, char *, ULONG), void *ctx)
{ (void)usage; (void)count; (void)make; (void)state; (void)insert; (void)ctx; assert(Locked); ++Translations; return HIDP_STATUS_SUCCESS; }
static void KeCancelTimer(int *timer) { (void)timer; assert(Locked); ++CancelCalls; }
static void KbdHid_ArmRepeat(KBDHID_DEVICE_EXTENSION *dev, BOOLEAN first) { (void)dev; (void)first; assert(Locked); ++Arms; }
static void KbdHid_UpdateRepeat(KBDHID_DEVICE_EXTENSION *);
static void KeFlushQueuedDpcs(void)
{
    ++FlushCalls;
    assert(!Locked);
    if (InjectCompletion)
    {
        KIRQL old;
        InjectCompletion=0;
        KeAcquireSpinLock(&InjectedDevice->InputLock, &old);
        KbdHid_UpdateRepeat(InjectedDevice);
        KeReleaseSpinLock(&InjectedDevice->InputLock, old);
    }
}
static int IoCancelIrp(void *irp) { (void)irp; assert(!Locked); return 0; }
static int KeWaitForSingleObject(void *event, int reason, int mode, int alert, void *timeout) { (void)event; (void)reason; (void)mode; (void)alert; (void)timeout; return 0; }
''' + '\n'.join(functions) + r'''
int main(void)
{
    KBDHID_DEVICE_EXTENSION dev = {0};
    KIRQL old;
    dev.UsageListLength=2;
    dev.MakeUsageList[0].UsagePage=HID_USAGE_PAGE_KEYBOARD;
    dev.MakeUsageList[0].Usage=4;
    dev.RepeatEnabled=TRUE;
    dev.RepeatWanted=TRUE;
    dev.DeviceStarted=TRUE;
    KeAcquireSpinLock(&dev.InputLock, &old);
    KbdHid_UpdateRepeat(&dev);
    KeReleaseSpinLock(&dev.InputLock, old);
    assert(Arms==1 && dev.RepeatUsage.Usage==4);
    Arms=0;
    InjectedDevice=&dev;
    InjectCompletion=1;
    KbdHid_StopRead(&dev, TRUE);
    if (Arms || dev.RepeatUsage.Usage || dev.RepeatEnabled)
    {
        fprintf(stderr, "report completion rearmed repeat during shutdown: arms=%d usage=%u enabled=%u\n", Arms, dev.RepeatUsage.Usage, dev.RepeatEnabled);
        return 1;
    }
    assert(CancelCalls && FlushCalls==1 && !Locked && !dev.DeviceStarted && dev.RepeatWanted);
    dev.RepeatUsage.Usage=4;
    KbdHid_RepeatDpc(NULL, &dev, NULL, NULL);
    assert(!Translations && !Arms);
    dev.RepeatUsage.Usage=0;
    KbdHid_EnableRepeat(&dev);
    assert(!dev.RepeatEnabled);
    KbdHid_MarkStarted(&dev);
    assert(dev.RepeatEnabled && dev.RepeatWanted);
    KeAcquireSpinLock(&dev.InputLock, &old);
    KbdHid_UpdateRepeat(&dev);
    KeReleaseSpinLock(&dev.InputLock, old);
    assert(Arms==1 && dev.RepeatUsage.Usage==4);
    KbdHid_RepeatDpc(NULL, &dev, NULL, NULL);
    assert(Translations==1 && Arms==2);
    KbdHid_StopRead(&dev, FALSE);
    assert(dev.DeviceStarted && !dev.RepeatWanted && !dev.RepeatEnabled && dev.RepeatUsage.Usage==0);
    KbdHid_MarkStarted(&dev);
    assert(!dev.RepeatEnabled);
    KbdHid_EnableRepeat(&dev);
    assert(dev.RepeatEnabled && dev.RepeatWanted);
    puts("keyboard HID stop prevents repeat rearm");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='kbdhid-repeat-stop-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
