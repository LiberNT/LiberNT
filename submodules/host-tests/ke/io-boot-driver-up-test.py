# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A boot driver marked to run on one processor only stops a multiprocessor system before it starts
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)
sources = {
    'drvinit.c': (root / 'ntos/io/iomgr/drvinit.c').read_text(),
    'ntsysldr.c': (root / 'ntos/mm/nt/ntsysldr.c').read_text(),
}


def definition(file, name):
    source = sources[file]
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
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define VOID void
#define NTAPI
#define FASTCALL
#define FORCEINLINE static inline
#define CODE_SEG(x)
#define _In_
#define TRUE 1
#define FALSE 0
#define PAGED_CODE()
#define DPRINT1(...) ((void)0)
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define IMAGE_FILE_UP_SYSTEM_ONLY 0x4000
#define UP_DRIVER_ON_MP_SYSTEM 0x92
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
typedef unsigned char BOOLEAN;
typedef char CCHAR;
typedef unsigned short USHORT;
typedef uint32_t ULONG;
typedef int32_t NTSTATUS;
typedef uintptr_t ULONG_PTR;
typedef void *PVOID, *HANDLE;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct { USHORT Length, MaximumLength; void *Buffer; } UNICODE_STRING, *PUNICODE_STRING;
typedef struct { USHORT Machine; USHORT Characteristics; } IMAGE_FILE_HEADER;
typedef struct { IMAGE_FILE_HEADER FileHeader; } IMAGE_NT_HEADERS, *PIMAGE_NT_HEADERS;
typedef struct { PVOID DllBase; } LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;
typedef struct { LIST_ENTRY Link; UNICODE_STRING FilePath; UNICODE_STRING RegistryPath; PLDR_DATA_TABLE_ENTRY LdrEntry; } BOOT_DRIVER_LIST_ENTRY, *PBOOT_DRIVER_LIST_ENTRY;
typedef struct { LIST_ENTRY BootDriverListHead; } LOADER_PARAMETER_BLOCK;
typedef struct _DRIVER_OBJECT *PDRIVER_OBJECT;
static const char *Scenario = "setup";
static CCHAR KeNumberProcessors;
static LOADER_PARAMETER_BLOCK LoaderBlock, *IopLoaderBlock = &LoaderBlock;
static BOOLEAN PnPBootDriversLoaded;
static IMAGE_NT_HEADERS Image;
static LDR_DATA_TABLE_ENTRY Module = { &Image };
static BOOT_DRIVER_LIST_ENTRY Driver;
static int Started;
static ULONG BugCheckCode;
static ULONG_PTR BugCheckParameter;
static jmp_buf Stopped;
static NTSTATUS PiInitCacheGroupInformation(void) { return STATUS_SUCCESS; }
static NTSTATUS IopOpenServiceKeyForPath(PUNICODE_STRING Path, HANDLE *Handle) { (void)Path; *Handle = (HANDLE)1; return STATUS_SUCCESS; }
static NTSTATUS ZwClose(HANDLE Handle) { (void)Handle; return STATUS_SUCCESS; }
static void IopBootLog(PUNICODE_STRING Path, BOOLEAN Loaded) { (void)Path; (void)Loaded; }
static void ObDereferenceObject(PVOID Object) { (void)Object; }
static NTSTATUS IopInitializePlugPlayServices(void) { return STATUS_SUCCESS; }
static void IopReinitializeBootDrivers(void) {}
static PIMAGE_NT_HEADERS RtlImageNtHeader(PVOID Base) { return Base; }
static NTSTATUS IopInitializeDriverModule(PLDR_DATA_TABLE_ENTRY Entry, HANDLE Key, PDRIVER_OBJECT *Object, NTSTATUS *EntryStatus, BOOLEAN Pnp) {
    (void)Key; (void)Pnp;
    CHECK(Entry == &Module);
    Started++;
    *Object = NULL;
    *EntryStatus = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}
static void KeBugCheckEx(ULONG Code, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3, ULONG_PTR P4) {
    CHECK(P2 == 0 && P3 == 0 && P4 == 0);
    BugCheckCode = Code;
    BugCheckParameter = P1;
    longjmp(Stopped, 1);
}
'''
fixture += definition('ntsysldr.c', 'MiVerifyImageIsOkForMpUse')
fixture += definition('ntsysldr.c', 'MmVerifyImageIsOkForMpUse')
fixture += definition('drvinit.c', 'IopInitializeBootDrivers')
fixture += r'''
static void Boot(const char *Name, CCHAR Processors, USHORT Characteristics) {
    Scenario = Name;
    KeNumberProcessors = Processors;
    Image.FileHeader.Characteristics = Characteristics;
    Driver.LdrEntry = &Module;
    LoaderBlock.BootDriverListHead.Flink = LoaderBlock.BootDriverListHead.Blink = &Driver.Link;
    Driver.Link.Flink = Driver.Link.Blink = &LoaderBlock.BootDriverListHead;
    Started = 0;
    BugCheckCode = 0;
    BugCheckParameter = 0;
    if (!setjmp(Stopped))
        IopInitializeBootDrivers();
}
int main(void) {
    Boot("one processor", 1, IMAGE_FILE_UP_SYSTEM_ONLY);
    CHECK(BugCheckCode == 0);
    CHECK(Started == 1);

    Boot("two processors, ordinary driver", 2, 0);
    CHECK(BugCheckCode == 0);
    CHECK(Started == 1);

    Boot("two processors, uniprocessor-only driver", 2, IMAGE_FILE_UP_SYSTEM_ONLY);
    CHECK(BugCheckCode == UP_DRIVER_ON_MP_SYSTEM);
    CHECK(BugCheckParameter == (ULONG_PTR)Module.DllBase);
    CHECK(Started == 0);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='io-boot-driver-up-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
