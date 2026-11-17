# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     TDI_ASSOCIATE_ADDRESS reads the file object it refuses before giving up its reference to it
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--source', help='dispatch.c to test instead of the one under --root')
args = parser.parse_args()
source = pathlib.Path(args.source or pathlib.Path(args.root) / 'drivers/network/tcpip/tcpip/dispatch.c').read_text()

match = re.search(r'(?ms)^NTSTATUS DispTdiAssociateAddress\(.*?^\}\n', source)
assert match, 'DispTdiAssociateAddress not found'

fixture = r'''
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define NT_SUCCESS(s) ((s) >= 0)
#define KernelMode 0
#define DEBUG_IRP 1
#define MID_TRACE 2
#define TDI_TRANSPORT_ADDRESS_FILE 1
#define GONE ((PVOID)(uintptr_t)0xDEAD0BAD)
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); Failures++; } } while (0)
/* The debug build's trace evaluates its arguments, as this one does */
#define TI_DbgPrint(_t_, _x_) Trace _x_
typedef int NTSTATUS;
typedef void *PVOID, *HANDLE;
typedef unsigned char BOOLEAN;
typedef unsigned int UINT;
typedef struct _FILE_OBJECT { PVOID FsContext, FsContext2; int References; } FILE_OBJECT, *PFILE_OBJECT;
typedef struct { struct { PVOID ConnectionContext, AddressHandle; } Handle; } TRANSPORT_CONTEXT, *PTRANSPORT_CONTEXT;
typedef struct { HANDLE AddressHandle; } TDI_REQUEST_KERNEL_ASSOCIATE, *PTDI_REQUEST_KERNEL_ASSOCIATE;
typedef struct { PFILE_OBJECT FileObject; TDI_REQUEST_KERNEL_ASSOCIATE Parameters; } IO_STACK_LOCATION, *PIO_STACK_LOCATION;
typedef struct { IO_STACK_LOCATION Stack; } IRP, *PIRP;
struct _ADDRESS_FILE;
typedef struct _CONNECTION_ENDPOINT
{
    struct _ADDRESS_FILE *AddressFile;
    struct _CONNECTION_ENDPOINT *Next;
    int Locked;
} CONNECTION_ENDPOINT, *PCONNECTION_ENDPOINT;
typedef struct _ADDRESS_FILE
{
    PCONNECTION_ENDPOINT Connection;
    BOOLEAN PendingKeepAliveSet, PendingNoDelaySet;
    UINT PendingKeepAlive;
    BOOLEAN PendingNoDelay;
} ADDRESS_FILE, *PADDRESS_FILE;

static int Failures;
static PVOID Traced;
static int TracedMagic;
static PVOID FileType, *IoFileObjectType = &FileType;
static PFILE_OBJECT Opened;

static void Trace(const char *Format, ...)
{
    va_list Args;

    if (!strstr(Format, "Magic"))
        return;
    va_start(Args, Format);
    Traced = va_arg(Args, PVOID);
    va_end(Args);
    TracedMagic = 1;
}

static PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP Irp) { return &Irp->Stack; }
static NTSTATUS ObReferenceObjectByHandle(HANDLE Handle, int Access, PVOID Type, int Mode, PVOID *Object, PVOID Info)
{
    (void)Handle; (void)Access; (void)Type; (void)Mode; (void)Info;
    Opened->References++;
    *Object = Opened;
    return STATUS_SUCCESS;
}
/* The last reference going away frees the object: what was in it is gone */
static void ObDereferenceObject(PFILE_OBJECT Object)
{
    if (--Object->References == 0)
        Object->FsContext = Object->FsContext2 = GONE;
}
static void LockObject(PVOID Object) { (void)Object; }
static void UnlockObject(PVOID Object) { (void)Object; }
static void ReferenceObject(PVOID Object) { (void)Object; }
static void TCPSetKeepAlive(PCONNECTION_ENDPOINT Connection, UINT Value) { (void)Connection; (void)Value; }
static void TCPSetNoDelay(PCONNECTION_ENDPOINT Connection, BOOLEAN Value) { (void)Connection; (void)Value; }

''' + match.group(0) + r'''
int main(void)
{
    CONNECTION_ENDPOINT Connection = { NULL, NULL, 0 };
    TRANSPORT_CONTEXT ConnectionContext = { { &Connection, NULL } };
    FILE_OBJECT ConnectionFile = { &ConnectionContext, NULL, 1 };
    /* Not an address file, and its handle is closed while the request runs: the request holds the only reference */
    FILE_OBJECT Other = { NULL, (PVOID)(uintptr_t)0x77, 0 };
    IRP Irp;

    memset(&Irp, 0, sizeof(Irp));
    Irp.Stack.FileObject = &ConnectionFile;
    Opened = &Other;

    CHECK(DispTdiAssociateAddress(&Irp) == STATUS_INVALID_PARAMETER);
    CHECK(Other.References == 0);
    CHECK(Connection.AddressFile == NULL);
    CHECK(TracedMagic);
    CHECK(Traced == (PVOID)(uintptr_t)0x77);

    if (!Failures)
        printf("tcpip associate: a file object that is not an address file is refused and read only while referenced\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'dispatch.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wno-unused-function', '-Wno-unused-variable',
                    str(path / 'dispatch.c'), '-o', str(path / 'dispatch')], check=True)
    subprocess.run([str(path / 'dispatch')], check=True)
