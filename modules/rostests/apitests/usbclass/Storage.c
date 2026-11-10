/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test reading and writing a USB mass storage volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>
#include <winioctl.h>
#include <ntddstor.h>
#include <ntddscsi.h>

#define MARKER_NAME "USBCLASS.TXT"
#define MARKER_TEXT "LiberNT USB storage marker\r\n"

typedef struct _STORAGE_SPT
{
    SCSI_PASS_THROUGH_DIRECT Spt;
    UCHAR Sense[32];
} STORAGE_SPT;

static
BOOL
SendCdb(HANDLE Disk, const UCHAR *Cdb, UCHAR DataIn, PVOID Buffer, ULONG Length, ULONG Timeout,
        STORAGE_SPT *Request, DWORD *Error)
{
    DWORD Returned;
    BOOL Result;

    ZeroMemory(Request, sizeof(*Request));
    Request->Spt.Length = sizeof(Request->Spt);
    Request->Spt.CdbLength = 10;
    Request->Spt.SenseInfoLength = sizeof(Request->Sense);
    Request->Spt.DataIn = DataIn;
    Request->Spt.DataTransferLength = Length;
    Request->Spt.TimeOutValue = Timeout;
    Request->Spt.DataBuffer = Buffer;
    Request->Spt.SenseInfoOffset = FIELD_OFFSET(STORAGE_SPT, Sense);
    memcpy(Request->Spt.Cdb, Cdb, 10);
    SetLastError(0xdeadbeef);
    Result = DeviceIoControl(Disk, IOCTL_SCSI_PASS_THROUGH_DIRECT, Request, sizeof(*Request), Request,
                             sizeof(*Request), &Returned, NULL);
    *Error = Result ? ERROR_SUCCESS : GetLastError();
    return Result;
}

static
void
TestRecovery(const CHAR *Root)
{
    static const UCHAR ReadCapacity[10] = { 0x25 };
    UCHAR Read[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
    CHAR Path[MAX_PATH];
    STORAGE_DEVICE_NUMBER Number;
    STORAGE_SPT Request;
    PUCHAR First, Again;
    ULONGLONG Start, Elapsed;
    DWORD Returned, Error, BlockSize;
    HANDLE Volume, Disk;
    BOOL Result;

    sprintf(Path, "\\\\.\\%c:", Root[0]);
    Volume = CreateFileA(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(Volume != INVALID_HANDLE_VALUE, "Opening %s failed: %lu\n", Path, GetLastError());
    if (Volume == INVALID_HANDLE_VALUE)
        return;
    Result = DeviceIoControl(Volume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &Number, sizeof(Number), &Returned,
                             NULL);
    ok(Result, "IOCTL_STORAGE_GET_DEVICE_NUMBER failed: %lu\n", GetLastError());
    CloseHandle(Volume);
    if (!Result)
        return;
    sprintf(Path, "\\\\.\\PhysicalDrive%lu", Number.DeviceNumber);
    Disk = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
                       NULL);
    ok(Disk != INVALID_HANDLE_VALUE, "Opening %s failed: %lu\n", Path, GetLastError());
    if (Disk == INVALID_HANDLE_VALUE)
        return;
    First = VirtualAlloc(NULL, 2 * 4096, MEM_COMMIT, PAGE_READWRITE);
    Again = First + 4096;

    Result = SendCdb(Disk, ReadCapacity, SCSI_IOCTL_DATA_IN, First, 8, 10, &Request, &Error);
    ok(Result && Request.Spt.ScsiStatus == 0, "READ CAPACITY gave %d, error %lu, SCSI status %u\n", Result, Error,
       Request.Spt.ScsiStatus);
    BlockSize = (First[4] << 24) | (First[5] << 16) | (First[6] << 8) | First[7];
    ok(BlockSize == 512, "The block size is %lu\n", BlockSize);
    if (!Result || BlockSize == 0 || BlockSize > 4096)
        goto Done;
    memcpy(Read + 2, First, 4);
    Result = SendCdb(Disk, Read, SCSI_IOCTL_DATA_IN, First, BlockSize, 10, &Request, &Error);
    ok(Result && Request.Spt.ScsiStatus == 0 && Request.Spt.DataTransferLength == BlockSize,
       "READ gave %d, error %lu, SCSI status %u, %lu bytes\n", Result, Error, Request.Spt.ScsiStatus,
       Request.Spt.DataTransferLength);

    Start = GetTickCount64();
    Result = SendCdb(Disk, Read, SCSI_IOCTL_DATA_UNSPECIFIED, NULL, 0, 2, &Request, &Error);
    Elapsed = GetTickCount64() - Start;
    ok(!Result && ((Error == ERROR_SEM_TIMEOUT && Elapsed >= 1900) || Error == ERROR_IO_DEVICE),
       "A READ with no data phase gave %d, error %lu, SCSI status %u after %lu ms\n", Result, Error,
       Request.Spt.ScsiStatus, (ULONG)Elapsed);

    Result = SendCdb(Disk, Read, SCSI_IOCTL_DATA_IN, Again, BlockSize, 10, &Request, &Error);
    ok(Result && Request.Spt.ScsiStatus == 0 && Request.Spt.DataTransferLength == BlockSize &&
       !memcmp(First, Again, BlockSize), "The device did not recover: READ gave %d, error %lu, SCSI status %u\n",
       Result, Error, Request.Spt.ScsiStatus);
Done:
    VirtualFree(First, 0, MEM_RELEASE);
    CloseHandle(Disk);
}

/* The volume under test carries a marker file the harness put there. */
START_TEST(Storage)
{
    CHAR Root[] = "A:\\", Path[MAX_PATH], Buffer[256];
    static const CHAR Payload[] = "written through the USB storage stack";
    DWORD Drives = GetLogicalDrives(), Read, Written;
    HANDLE File;
    UINT Drive;
    BOOL Found = FALSE;

    for (Drive = 2; Drive < 26 && !Found; ++Drive)
    {
        if (!(Drives & (1u << Drive)))
            continue;
        Root[0] = (CHAR)('A' + Drive);
        if (GetDriveTypeA(Root) != DRIVE_REMOVABLE && GetDriveTypeA(Root) != DRIVE_FIXED)
            continue;
        sprintf(Path, "%s%s", Root, MARKER_NAME);
        File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (File == INVALID_HANDLE_VALUE)
            continue;
        Found = TRUE;
        ZeroMemory(Buffer, sizeof(Buffer));
        ok(ReadFile(File, Buffer, sizeof(Buffer) - 1, &Read, NULL), "Reading %s failed: %lu\n", Path, GetLastError());
        CloseHandle(File);
        ok(!strcmp(Buffer, MARKER_TEXT), "The marker on %s reads '%s'\n", Root, Buffer);

        sprintf(Path, "%sUSBWRITE.TXT", Root);
        File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, NULL);
        ok(File != INVALID_HANDLE_VALUE, "Creating %s failed: %lu\n", Path, GetLastError());
        if (File == INVALID_HANDLE_VALUE)
            break;
        ok(WriteFile(File, Payload, sizeof(Payload), &Written, NULL) && Written == sizeof(Payload),
           "Writing %s failed: %lu\n", Path, GetLastError());
        ok(FlushFileBuffers(File), "Flushing %s failed: %lu\n", Path, GetLastError());
        CloseHandle(File);
        File = CreateFileA(Path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
        ok(File != INVALID_HANDLE_VALUE, "Opening %s again failed: %lu\n", Path, GetLastError());
        if (File != INVALID_HANDLE_VALUE)
        {
            PVOID Aligned = VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);

            ok(ReadFile(File, Aligned, 4096, &Read, NULL), "Reading %s back failed: %lu\n", Path, GetLastError());
            ok(Read == sizeof(Payload) && !memcmp(Aligned, Payload, sizeof(Payload)),
               "%s read back %lu bytes\n", Path, Read);
            VirtualFree(Aligned, 0, MEM_RELEASE);
            CloseHandle(File);
        }
        ok(DeleteFileA(Path), "Deleting %s failed: %lu\n", Path, GetLastError());
        trace("USB storage volume %s read and written\n", Root);
        TestRecovery(Root);
    }
    if (!Found)
        skip("No volume carries %s\n", MARKER_NAME);
}
