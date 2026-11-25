/*
 * PROJECT:     LiberNT rostests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Disk sequential I/O throughput measurement
 */

/* benchdisk: sequential file I/O benchmark
 *   benchdisk <path> <mb>        write then read <mb> MiB, buffered
 *   benchdisk <path> <mb> -u     same with FILE_FLAG_NO_BUFFERING
 *   benchdisk <path> <mb> -s     write, fsync, delete (write path only)
 * prints single-line results via printf
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define CHUNK (1024 * 1024)

static double rate(ULONGLONG bytes, LARGE_INTEGER *f, LARGE_INTEGER *a, LARGE_INTEGER *b)
{
    double ms = (double)(b->QuadPart - a->QuadPart) * 1000.0 / (double)f->QuadPart;
    return ms > 0.0 ? (double)bytes / 1048576.0 / (ms / 1000.0) : 0.0;
}

int main(int argc, char **argv)
{
    char *path;
    ULONGLONG total, done;
    DWORD flags = FILE_ATTRIBUTE_NORMAL, n;
    int sync_only = 0;
    LARGE_INTEGER f, t0, t1, t2;
    HANDLE h;
    UCHAR *buf;
    int i;

    if (argc < 3)
    {
        printf("usage: benchdisk <path> <mb> [-u unbuffered] [-s write+fsync only]\n");
        return 2;
    }
    path = argv[1];
    total = (ULONGLONG)atoll(argv[2]) << 20;
    for (i = 3; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] == 'u')
            flags = FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
        else if (argv[i][0] == '-' && argv[i][1] == 's')
            sync_only = 1;
    }

    buf = (UCHAR *)VirtualAlloc(NULL, CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf)
    {
        printf("alloc failed\n");
        return 1;
    }
    memset(buf, 0xC3, CHUNK);
    QueryPerformanceFrequency(&f);

    h = CreateFileA(path, GENERIC_WRITE | GENERIC_READ, 0, NULL,
                    CREATE_ALWAYS, flags, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        printf("create %s gle=%lu\n", path, GetLastError());
        return 1;
    }

    QueryPerformanceCounter(&t0);
    for (done = 0; done < total; done += n)
    {
        DWORD want = (DWORD)((total - done) > CHUNK ? CHUNK : (total - done));
        if (!WriteFile(h, buf, want, &n, NULL) || n != want)
        {
            printf("write gle=%lu at %llu\n", GetLastError(), done);
            CloseHandle(h);
            return 1;
        }
    }
    FlushFileBuffers(h);
    QueryPerformanceCounter(&t1);
    printf("wr %llu bytes %.0f ms %.2f MB/s\n", done,
           (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart,
           rate(done, &f, &t0, &t1));

    if (sync_only)
    {
        CloseHandle(h);
        DeleteFileA(path);
        return 0;
    }

    if (!(flags & FILE_FLAG_NO_BUFFERING))
    {
        /* reopen unbuffered so the read path hits the device, not the cache */
        CloseHandle(h);
        h = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
        if (h == INVALID_HANDLE_VALUE)
        {
            printf("reopen gle=%lu\n", GetLastError());
            return 1;
        }
    }
    else
    {
        SetFilePointer(h, 0, NULL, FILE_BEGIN);
    }
    for (done = 0; done < total; done += n)
    {
        DWORD want = (DWORD)((total - done) > CHUNK ? CHUNK : (total - done));
        if (!ReadFile(h, buf, want, &n, NULL) || n != want)
        {
            if (!n) break;
            printf("read gle=%lu at %llu\n", GetLastError(), done);
            break;
        }
    }
    QueryPerformanceCounter(&t2);
    printf("rd %llu bytes %.0f ms %.2f MB/s\n", done,
           (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / (double)f.QuadPart,
           rate(done, &f, &t1, &t2));

    CloseHandle(h);
    DeleteFileA(path);
    VirtualFree(buf, 0, MEM_RELEASE);
    return 0;
}
