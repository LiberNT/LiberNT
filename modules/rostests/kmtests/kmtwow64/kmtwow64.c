/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sends one device I/O control request from a WoW64 process and prints the reply
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>

int
wmain(int argc, WCHAR **argv)
{
    UCHAR Buffer[256];
    HANDLE Device;
    ULONG Code;
    DWORD OutSize;
    DWORD Returned = 0;
    DWORD Error;
    DWORD i;

    if (argc != 4)
    {
        printf("usage: kmtwow64 <device path> <ioctl code in hex> <output size>\n");
        return ERROR_INVALID_PARAMETER;
    }

    Code = wcstoul(argv[2], NULL, 16);
    OutSize = wcstoul(argv[3], NULL, 10);
    if (OutSize > sizeof(Buffer))
    {
        printf("KMTWOW64 output size %lu above %lu\n", OutSize, (DWORD)sizeof(Buffer));
        return ERROR_INVALID_PARAMETER;
    }

    Device = CreateFileW(argv[1], GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Device == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
        printf("KMTWOW64 open error=%lu\n", Error);
        return Error;
    }

    if (!DeviceIoControl(Device, Code, NULL, 0, Buffer, OutSize, &Returned, NULL))
    {
        Error = GetLastError();
        printf("KMTWOW64 ioctl error=%lu\n", Error);
        CloseHandle(Device);
        return Error;
    }
    CloseHandle(Device);

    printf("KMTWOW64 bytes=%lu data=", Returned);
    for (i = 0; i < Returned; i++)
        printf("%02x", Buffer[i]);
    printf("\n");
    return ERROR_SUCCESS;
}
