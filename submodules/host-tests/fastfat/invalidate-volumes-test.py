# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     FSCTL_INVALIDATE_VOLUMES reads the device of the handle's file object while it still references it
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--source', help='fsctrl.c to test instead of the one under --root')
args = parser.parse_args()
source = pathlib.Path(args.source or pathlib.Path(args.root) / 'drivers/filesystems/fastfat/fsctrl.c').read_text(errors='replace')

# The prototype near the top of the file is spelt the same way: the definition is the last one
start = [m.start() for m in re.finditer(r'(?m)^NTSTATUS\nFatInvalidateVolumes \(', source)][-1]
match = re.compile(r'(?ms)^NTSTATUS\nFatInvalidateVolumes \(.*?^\}\n').match(source, start)
assert match, 'FatInvalidateVolumes not found'

fixture = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __REACTOS__
#define IN
#define TRUE 1
#define FALSE 0
#define VOID void
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_PRIVILEGE_NOT_HELD ((NTSTATUS)0xC0000061)
#define NT_SUCCESS(s) ((s) >= 0)
#define NT_ASSERT(x) ((void)0)
#define SE_TCB_PRIVILEGE 7
#define KernelMode 0
#define IRP_CONTEXT_FLAG_WAIT 1
#define VPB_MOUNTED 1
#define VPB_REMOVE_PENDING 8
#define VCB_STATE_FLAG_VPB_MUST_BE_FREED 0x40
#define IO_TYPE_VPB 10
#define FatNull NULL
#define NoFlush 0
#define VcbBad 3
#define FcbBad 3
#define Dbg 0
#define DebugTrace(a, b, c, d) ((void)0)
#define SetFlag(f, v) ((f) |= (v))
#define FlagOn(f, v) ((f) & (v))
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define GONE ((PDEVICE_OBJECT)(uintptr_t)0xDEAD0BAD)
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); Failures++; } } while (0)
typedef int NTSTATUS;
typedef unsigned char BOOLEAN, KIRQL;
typedef unsigned int ULONG;
typedef void *PVOID, *HANDLE, **PHANDLE;
typedef struct { ULONG LowPart; int HighPart; } LUID;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
struct _VPB;
typedef struct _DEVICE_OBJECT { struct _VPB *Vpb; } DEVICE_OBJECT, *PDEVICE_OBJECT;
typedef struct _VPB { short Type, Size; ULONG Flags; PDEVICE_OBJECT DeviceObject, RealDevice; } VPB, *PVPB;
typedef struct _FILE_OBJECT { PDEVICE_OBJECT DeviceObject; int References; } FILE_OBJECT, *PFILE_OBJECT;
typedef struct
{
    struct { struct { ULONG InputBufferLength; } FileSystemControl; } Parameters;
    unsigned char MajorFunction, MinorFunction;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
typedef struct { char RequestorMode; struct { PVOID SystemBuffer; } AssociatedIrp; IO_STACK_LOCATION Stack; } IRP, *PIRP;
typedef struct { ULONG Flags; unsigned char MajorFunction, MinorFunction; } IRP_CONTEXT, *PIRP_CONTEXT;
typedef struct _VCB
{
    LIST_ENTRY VcbLinks;
    PVPB Vpb, SwapVpb;
    ULONG VcbState, OpenFileCount;
    PVOID RootDcb;
    int Condition;
} VCB, *PVCB;

static int Failures, Completed;
static NTSTATUS CompletedWith;
static struct { LIST_ENTRY VcbQueue; } FatData;
static PVOID FileType, *IoFileObjectType = &FileType;
static PFILE_OBJECT Opened;

static PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP Irp) { return &Irp->Stack; }
static BOOLEAN SeSinglePrivilegeCheck(LUID Privilege, char Mode) { (void)Privilege; (void)Mode; return TRUE; }
static void FatCompleteRequest(PVOID Context, PIRP Irp, NTSTATUS Status) { (void)Context; (void)Irp; Completed++; CompletedWith = Status; }
static NTSTATUS ObReferenceObjectByHandle(HANDLE Handle, ULONG Access, PVOID Type, int Mode, PVOID *Object, PVOID Info)
{
    (void)Handle; (void)Access; (void)Type; (void)Mode; (void)Info;
    Opened->References++;
    *Object = Opened;
    return STATUS_SUCCESS;
}
/* The last reference going away frees the file object: what was in it is gone */
static void ObDereferenceObject(PFILE_OBJECT Object)
{
    if (--Object->References == 0)
        Object->DeviceObject = GONE;
}
static void FatAcquireExclusiveGlobal(PIRP_CONTEXT Context) { (void)Context; }
static void FatReleaseGlobal(PIRP_CONTEXT Context) { (void)Context; }
static BOOLEAN FatAcquireExclusiveVcb(PIRP_CONTEXT Context, PVCB Vcb) { (void)Context; (void)Vcb; return TRUE; }
static void FatReleaseVcb(PIRP_CONTEXT Context, PVCB Vcb) { (void)Context; (void)Vcb; }
static void IoAcquireVpbSpinLock(KIRQL *Irql) { *Irql = 0; }
static void IoReleaseVpbSpinLock(KIRQL Irql) { (void)Irql; }
static void FatSetVcbCondition(PVCB Vcb, int Condition) { Vcb->Condition = Condition; }
static void FatAcquireExclusiveVolume(PIRP_CONTEXT Context, PVCB Vcb) { (void)Context; (void)Vcb; }
static void FatReleaseVolume(PIRP_CONTEXT Context, PVCB Vcb) { (void)Context; (void)Vcb; }
static void FatMarkFcbCondition(PIRP_CONTEXT Context, PVOID Fcb, int Condition, BOOLEAN Recursive) { (void)Context; (void)Fcb; (void)Condition; (void)Recursive; }
static void FatPurgeReferencedFileObjects(PIRP_CONTEXT Context, PVOID Fcb, int Flush) { (void)Context; (void)Fcb; (void)Flush; }
static BOOLEAN FatCheckForDismount(PIRP_CONTEXT Context, PVCB Vcb, BOOLEAN Force) { (void)Context; (void)Vcb; (void)Force; return FALSE; }

''' + match.group(0) + r'''
int main(void)
{
    VPB Mounted = { 0, 0, 0, NULL, NULL }, Other = { 0, 0, 0, NULL, NULL };
    DEVICE_OBJECT Disk = { &Mounted }, OtherDisk = { &Other };
    /* The handle is closed while the request runs: the request holds the only reference */
    FILE_OBJECT Volume = { &Disk, 0 };
    VCB OnDisk, Elsewhere;
    HANDLE Handle = (HANDLE)(uintptr_t)4;
    IRP Irp;

    memset(&OnDisk, 0, sizeof(OnDisk));
    memset(&Elsewhere, 0, sizeof(Elsewhere));
    Mounted.RealDevice = &Disk;
    Other.RealDevice = &OtherDisk;
    OnDisk.Vpb = &Mounted;
    Elsewhere.Vpb = &Other;
    FatData.VcbQueue.Flink = &OnDisk.VcbLinks;
    OnDisk.VcbLinks.Flink = &Elsewhere.VcbLinks;
    Elsewhere.VcbLinks.Flink = &FatData.VcbQueue;

    memset(&Irp, 0, sizeof(Irp));
    Irp.AssociatedIrp.SystemBuffer = &Handle;
    Irp.Stack.Parameters.FileSystemControl.InputBufferLength = sizeof(HANDLE);
    Opened = &Volume;

    CHECK(FatInvalidateVolumes(&Irp) == STATUS_SUCCESS);
    CHECK(Volume.References == 0);
    CHECK(Completed == 1 && CompletedWith == STATUS_SUCCESS);
    /* The volume on the handle's device is marked bad, the one on another device is left alone */
    CHECK(OnDisk.Condition == VcbBad);
    CHECK(Elsewhere.Condition == 0);

    if (!Failures)
        printf("fastfat invalidate volumes: the volumes on the handle's device are marked bad, and only those\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'fsctrl.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unknown-pragmas',
                    str(path / 'fsctrl.c'), '-o', str(path / 'fsctrl')], check=True)
    subprocess.run([str(path / 'fsctrl')], check=True)
