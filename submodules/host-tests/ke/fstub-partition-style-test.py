# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A disk is GPT only behind a protective MBR; a legacy MBR hides any leftover GPT
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)

source = (root / 'ntos/fstub/disklayout.c').read_text()
start = source.index('NTSTATUS\nNTAPI\nFstubDetectPartitionStyle(')
function = source[start:source.index('\n}\n', start) + 3]
private = (root / 'ntos/fstub/fstubp.h').read_text()
defines = '\n'.join(re.findall(r'^#define FSTUB_(?:MBR_ENTRY_COUNT|PARTITION_TABLE_OFFSET|BOOT_SIGNATURE_OFFSET|BOOT_SIGNATURE)\s+\S+$',
                               private, re.M))
hal = (root / 'ntos/include/internal/hal.h').read_text()
descriptor = re.search(r'typedef struct _PARTITION_DESCRIPTOR\s*\{.*?\} PARTITION_DESCRIPTOR, \*PPARTITION_DESCRIPTOR;', hal, re.S).group(0)

fixture = r'''
#include <stdio.h>
#include <string.h>
typedef unsigned char UCHAR, *PUCHAR, BOOLEAN;
typedef unsigned short USHORT, *PUSHORT;
typedef unsigned int ULONG;
typedef unsigned long long ULONGLONG;
typedef int NTSTATUS;
typedef void *PVOID;
typedef const char *PCSTR;
#define NTAPI
#define _In_
#define _Out_
#define _Out_writes_bytes_(x)
#define PAGED_CODE()
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_DISK_CORRUPT_ERROR ((NTSTATUS)0xC0000032)
#define PARTITION_GPT 0xEE
typedef enum _PARTITION_STYLE { PARTITION_STYLE_MBR, PARTITION_STYLE_GPT, PARTITION_STYLE_RAW } PARTITION_STYLE;
typedef struct _FSTUB_DISK { ULONG BytesPerSector; PUCHAR Sector; } FSTUB_DISK, *PFSTUB_DISK;
typedef struct _EFI_PARTITION_HEADER { ULONGLONG Signature; } EFI_PARTITION_HEADER, *PEFI_PARTITION_HEADER;
@DEFINES@
@DESCRIPTOR@
static UCHAR Mbr[512];
static BOOLEAN PrimaryGpt, BackupGpt;
static NTSTATUS NTAPI FstubReadSector(PFSTUB_DISK Disk, ULONG SectorCount, ULONGLONG Lba, PVOID Buffer)
{
    (void)Disk;
    if (SectorCount != 1 || Lba != 0)
        return STATUS_DISK_CORRUPT_ERROR;
    memcpy(Buffer, Mbr, sizeof(Mbr));
    return STATUS_SUCCESS;
}
static NTSTATUS NTAPI FstubReadHeaderEFI(PFSTUB_DISK Disk, BOOLEAN ReadBackupTable, PEFI_PARTITION_HEADER Header)
{
    (void)Disk;
    Header->Signature = 0;
    return (ReadBackupTable ? BackupGpt : PrimaryGpt) ? STATUS_SUCCESS : STATUS_DISK_CORRUPT_ERROR;
}
@FUNCTION@
static int Failures;
static void Check(PCSTR Name, int Signature, int Type, BOOLEAN Primary, BOOLEAN Backup, PARTITION_STYLE Expected)
{
    UCHAR Sector[512];
    FSTUB_DISK Disk = { sizeof(Sector), Sector };
    PARTITION_STYLE Style = (PARTITION_STYLE)-1;
    NTSTATUS Status;

    memset(Mbr, 0, sizeof(Mbr));
    if (Signature)
    {
        Mbr[510] = 0x55;
        Mbr[511] = 0xAA;
    }
    Mbr[0x1BE + 4] = (UCHAR)Type;
    PrimaryGpt = Primary;
    BackupGpt = Backup;
    Status = FstubDetectPartitionStyle(&Disk, &Style);
    if (!NT_SUCCESS(Status) || Style != Expected)
    {
        printf("FAIL %s: status %#x style %d, expected style %d\n", Name, (unsigned)Status, (int)Style, (int)Expected);
        Failures++;
    }
}
int main(void)
{
    Check("legacy MBR", 1, 0x07, FALSE, FALSE, PARTITION_STYLE_MBR);
    Check("legacy MBR over a leftover backup GPT", 1, 0x07, FALSE, TRUE, PARTITION_STYLE_MBR);
    Check("legacy MBR over a leftover primary GPT", 1, 0xEF, TRUE, TRUE, PARTITION_STYLE_MBR);
    Check("protective MBR with GPT", 1, PARTITION_GPT, TRUE, TRUE, PARTITION_STYLE_GPT);
    Check("protective MBR with only the backup GPT", 1, PARTITION_GPT, FALSE, TRUE, PARTITION_STYLE_GPT);
    Check("protective MBR without GPT", 1, PARTITION_GPT, FALSE, FALSE, PARTITION_STYLE_RAW);
    Check("blank disk", 0, 0, FALSE, FALSE, PARTITION_STYLE_RAW);
    if (!Failures)
        printf("fstub-partition-style: every disk gets the expected partition style\n");
    return Failures != 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'style.c').write_text(fixture.replace('@DEFINES@', defines).replace('@DESCRIPTOR@', descriptor)
                                  .replace('@FUNCTION@', function))
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path / 'style.c'), '-o', str(path / 'style')],
                   check=True)
    subprocess.run([str(path / 'style')], check=True)
