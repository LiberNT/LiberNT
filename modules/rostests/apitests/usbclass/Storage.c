/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test reading and writing a USB mass storage volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>

#define MARKER_NAME "USBCLASS.TXT"
#define MARKER_TEXT "LiberNT USB storage marker\r\n"

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
    }
    if (!Found)
        skip("No volume carries %s\n", MARKER_NAME);
}
