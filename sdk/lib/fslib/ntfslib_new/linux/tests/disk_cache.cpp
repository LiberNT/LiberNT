/*
 * PROJECT:     LiberNT NTFS tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Verify cached metadata reads across writes and a partial final page
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <cstdio>
#include <unistd.h>
#include <ntfs_linux.h>
#include <ntfslib_new_internal.h>

int main()
{
    const ULONG Page = 4096;
    const ULONG End = 6 * Page + 1;
    FILE* Image = tmpfile();
    if (!Image)
        return 1;

    int Fd = fileno(Image);
    char Path[64];
    UCHAR Boot[512] = {};
    Boot[12] = 2;
    snprintf(Path, sizeof(Path), "/proc/self/fd/%d", Fd);
    if (pwrite(Fd, Boot, sizeof(Boot), 0) != sizeof(Boot) || ftruncate(Fd, End) ||
        !NT_SUCCESS(NtfsDiskInitializeLinuxWritable(Path, nullptr)))
    {
        fclose(Image);
        return 1;
    }

    UCHAR Byte;
    UCHAR Updated = 0x65;
    bool Ok = NT_SUCCESS(NtfsReadVolume(Page, 1, &Byte)) && Byte == 0 &&
              NT_SUCCESS(NtfsReadVolume(2 * Page, 1, &Byte)) && Byte == 0 &&
              NT_SUCCESS(NtfsWriteVolume(4 * Page, 1, &Updated)) &&
              NT_SUCCESS(NtfsReadVolume(4 * Page, 1, &Byte)) && Byte == Updated &&
              NT_SUCCESS(NtfsReadVolume(5 * Page, 1, &Byte)) && Byte == 0 &&
              NT_SUCCESS(NtfsReadVolume(End - 1, 1, &Byte)) && Byte == 0 &&
              NtfsReadVolume(End, 1, &Byte) == STATUS_END_OF_FILE &&
              NT_SUCCESS(NtfsWriteVolume(End, 1, &Updated)) &&
              NT_SUCCESS(NtfsReadVolume(End, 1, &Byte)) && Byte == Updated;
    NtfsDiskCloseLinux();
    fclose(Image);
    return Ok ? 0 : 1;
}
