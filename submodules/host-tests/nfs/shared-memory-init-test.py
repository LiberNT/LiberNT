# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The NFS redirector hands the section it created to its caller, who is the one that closes it
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

match = re.search(r'(?ms)^NTSTATUS SharedMemoryInit\(.*?^\}\n', source)
assert match, 'SharedMemoryInit not found'

fixture = r'''
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define OUT
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001)
#define STATUS_OBJECT_NAME_COLLISION ((NTSTATUS)0xC0000035)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define SECURITY_DESCRIPTOR_REVISION 1
#define SECTION_MAP_READ 4
#define SECTION_MAP_WRITE 2
#define PAGE_READWRITE 4
#define SEC_COMMIT 0x8000000
#define NFS41_SHARED_MEMORY_NAME L"\\BaseNamedObjects\\nfs41_shared_memory"
#define DbgEn()
#define DbgEx()
#define DbgP(...)
#define print_error(...)
#define InitializeObjectAttributes(a, n, f, r, s) ((void)0)
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); Failures++; } } while (0)
typedef int NTSTATUS;
typedef unsigned int ULONG;
typedef void *PVOID, *HANDLE, **PHANDLE;
typedef struct { int Unused; } UNICODE_STRING, SECURITY_DESCRIPTOR, OBJECT_ATTRIBUTES;
typedef struct { long long QuadPart; } LARGE_INTEGER;
typedef struct { int Unused; } NFS41NP_SHARED_MEMORY;

static int Failures;
static NTSTATUS CreateAnswers;
#define CREATED ((HANDLE)(uintptr_t)0x1234)

static void RtlInitUnicodeString(UNICODE_STRING *String, const wchar_t *Text) { (void)String; (void)Text; }
static NTSTATUS RtlCreateSecurityDescriptor(SECURITY_DESCRIPTOR *Descriptor, ULONG Revision) { (void)Descriptor; (void)Revision; return STATUS_SUCCESS; }
static NTSTATUS RtlSetDaclSecurityDescriptor(SECURITY_DESCRIPTOR *Descriptor, int Present, PVOID Dacl, int Defaulted) { (void)Descriptor; (void)Present; (void)Dacl; (void)Defaulted; return STATUS_SUCCESS; }
static NTSTATUS ZwCreateSection(PHANDLE Handle, ULONG Access, OBJECT_ATTRIBUTES *Attributes, LARGE_INTEGER *Size, ULONG Protect, ULONG Flags, HANDLE File)
{
    (void)Access; (void)Attributes; (void)Size; (void)Protect; (void)Flags; (void)File;
    if (CreateAnswers == STATUS_SUCCESS)
        *Handle = CREATED;
    return CreateAnswers;
}

''' + match.group(0) + r'''
int main(void)
{
    HANDLE Section;

    /* The section is created: the caller gets its handle, which is what it will close */
    Section = NULL;
    CreateAnswers = STATUS_SUCCESS;
    CHECK(SharedMemoryInit(&Section) == STATUS_SUCCESS);
    CHECK(Section == CREATED);

    /* Somebody created it already: nothing was opened here, and there is nothing to close */
    Section = NULL;
    CreateAnswers = STATUS_OBJECT_NAME_COLLISION;
    CHECK(SharedMemoryInit(&Section) == STATUS_SUCCESS);
    CHECK(Section == NULL);

    Section = NULL;
    CreateAnswers = STATUS_INSUFFICIENT_RESOURCES;
    CHECK(SharedMemoryInit(&Section) == STATUS_INSUFFICIENT_RESOURCES);
    CHECK(Section == NULL);

    if (!Failures)
        printf("nfs shared memory: the handle of the section that was created reaches the caller\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'nfs.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
                    str(path / 'nfs.c'), '-o', str(path / 'nfs')], check=True)
    subprocess.run([str(path / 'nfs')], check=True)
