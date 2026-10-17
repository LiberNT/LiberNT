/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Authenticated file transfer over the RDP drive redirection channel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

/*
 * Authenticated RDPDR file transfer. The client redirects a dedicated
 * ROSXFER staging folder; request/result files and all payload bytes travel
 * over that RDP virtual channel. No HTTP listener or shell command is used.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <freerdp/server/rdpdr.h>
#include <winpr/custom-crypto.h>
#include "transfer.h"

#define CHUNK (64 * 1024)
#define MANIFEST_LIMIT (4 * 1024 * 1024)
#define MAX_FILES 16384

struct RDP_TRANSFER
{
    RdpdrServerContext *Channel;
    HANDLE Stop, Ready, Reply, Thread;
    TRANSFER_LOG Log;
    WCHAR Root[MAX_PATH];
    UINT32 Device, File, Status, Length;
    BYTE Data[CHUNK];
    char LastJob[33];
};

static BOOL Stopping(RDP_TRANSFER *t)
{
    return WaitForSingleObject(t->Stop, 0) == WAIT_OBJECT_0;
}

static UINT DriveAdded(RdpdrServerContext *channel, const RdpdrDevice *device)
{
    RDP_TRANSFER *t = channel->data;
    if (memcmp(device->PreferredDosName, "ROSXFER", 7) || device->PreferredDosName[7])
        return CHANNEL_RC_OK;
    if (WaitForSingleObject(t->Ready, 0) == WAIT_OBJECT_0)
        return CHANNEL_RC_OK;
    t->Device = device->DeviceId;
    t->Log("File transfer share ROSXFER connected, device=%lu", (ULONG)t->Device);
    SetEvent(t->Ready);
    return CHANNEL_RC_OK;
}

static UINT DriveRemoved(RdpdrServerContext *channel, UINT32 id)
{
    RDP_TRANSFER *t = channel->data;
    if (WaitForSingleObject(t->Ready, 0) == WAIT_OBJECT_0 && id == t->Device)
        SetEvent(t->Stop);
    return CHANNEL_RC_OK;
}

static void OpenDone(RdpdrServerContext *c, void *data, UINT32 status, UINT32 device, UINT32 file)
{
    RDP_TRANSFER *t = data;
    (void)c; (void)device;
    t->Status = status; t->File = file; SetEvent(t->Reply);
}

static void ReadDone(RdpdrServerContext *c, void *data, UINT32 status, const char *buffer, UINT32 length)
{
    RDP_TRANSFER *t = data;
    (void)c;
    t->Status = status; t->Length = 0;
    if (length > sizeof(t->Data) || (length && !buffer))
        t->Status = ERROR_INVALID_DATA;
    else if (!status && length)
    { memcpy(t->Data, buffer, length); t->Length = length; }
    SetEvent(t->Reply);
}

static void WriteDone(RdpdrServerContext *c, void *data, UINT32 status, UINT32 length)
{
    RDP_TRANSFER *t = data;
    (void)c;
    t->Status = status; t->Length = length; SetEvent(t->Reply);
}

static void CloseDone(RdpdrServerContext *c, void *data, UINT32 status)
{
    RDP_TRANSFER *t = data;
    (void)c;
    t->Status = status; SetEvent(t->Reply);
}

/* Only the transfer worker sends requests; callbacks run on FreeRDP's
 * channel thread. A timed-out request ends this transfer session, so its
 * callback cannot be mistaken for a later operation. Storage stays alive
 * until both threads have joined. */
static DWORD Await(RDP_TRANSFER *t, UINT sent)
{
    HANDLE events[] = { t->Stop, t->Reply };
    DWORD wait;
    if (sent) { t->Log("File transfer request could not be sent: %u", sent); SetEvent(t->Stop); return sent; }
    wait = WaitForMultipleObjects(2, events, FALSE, 30000);
    if (wait == WAIT_OBJECT_0 + 1) return t->Status;
    t->Log("File transfer request got no reply: wait=%lu error=%lu", wait, GetLastError());
    SetEvent(t->Stop);
    return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED;
}

static DWORD RemoteOpen(RDP_TRANSFER *t, const char *path, BOOL write, UINT32 *file)
{
    DWORD status;
    ResetEvent(t->Reply);
    status = Await(t, t->Channel->DriveOpenFile(t->Channel, t, t->Device, path,
                       write ? GENERIC_WRITE : GENERIC_READ, write ? FILE_OVERWRITE_IF : FILE_OPEN));
    if (!status) *file = t->File;
    return status;
}

static DWORD RemoteRead(RDP_TRANSFER *t, UINT32 file, UINT64 offset, UINT32 size)
{
    ResetEvent(t->Reply);
    return Await(t, t->Channel->DriveReadFile(t->Channel, t, t->Device, file, size, offset));
}

static DWORD RemoteWrite(RDP_TRANSFER *t, UINT32 file, UINT64 offset, const void *data, UINT32 size)
{
    const char *bytes = data;
    while (size && !Stopping(t))
    {
        DWORD status;
        ResetEvent(t->Reply);
        status = Await(t, t->Channel->DriveWriteFile(t->Channel, t, t->Device, file, bytes, size, offset));
        if (status) return status;
        if (!t->Length || t->Length > size) return ERROR_WRITE_FAULT;
        bytes += t->Length; offset += t->Length; size -= t->Length;
    }
    return size ? ERROR_OPERATION_ABORTED : 0;
}

static DWORD RemoteClose(RDP_TRANSFER *t, UINT32 file)
{
    if (Stopping(t)) return ERROR_OPERATION_ABORTED;
    ResetEvent(t->Reply);
    return Await(t, t->Channel->DriveCloseFile(t->Channel, t, t->Device, file));
}

/* Relative UTF-8 names only. Reject Windows aliases, alternate streams,
 * traversal and device names rather than relying on path normalization. */
static BOOL RelativePath(const char *path, WCHAR *wide)
{
    WCHAR *part, *end;
    if (!*path || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, MAX_PATH))
        return FALSE;
    for (part = wide; *part; part = end + 1)
    {
        size_t n, stem;
        for (end = part; *end && *end != '/' && *end != '\\'; ++end)
            if (*end < 32 || wcschr(L":*?\"<>|", *end)) return FALSE;
        n = end - part;
        if (!n || part[n - 1] == '.' || part[n - 1] == ' ') return FALSE;
        for (stem = 0; stem < n && part[stem] != '.'; ++stem) {}
        if ((stem == 3 && (!_wcsnicmp(part, L"CON", 3) || !_wcsnicmp(part, L"PRN", 3) ||
                          !_wcsnicmp(part, L"AUX", 3) || !_wcsnicmp(part, L"NUL", 3))) ||
            (stem == 4 && (!_wcsnicmp(part, L"COM", 3) || !_wcsnicmp(part, L"LPT", 3)) &&
                          part[3] >= '1' && part[3] <= '9')) return FALSE;
        if (!*end) return TRUE;
        *end = '\\';
        if (!end[1]) return FALSE;
    }
    return FALSE;
}

/* Create/check every parent below the configured root, refusing reparse
 * points at each component. The test volume is controlled by this server;
 * local administrators are trusted, as with the desktop input channel. */
static DWORD LocalPath(RDP_TRANSFER *t, const char *relative, WCHAR *path, BOOL mkdirs)
{
    WCHAR name[MAX_PATH], *p;
    size_t root = wcslen(t->Root);
    DWORD attr;
    if (!RelativePath(relative, name) || root + wcslen(name) + 1 >= MAX_PATH)
        return ERROR_INVALID_NAME;
    wcscpy(path, t->Root); wcscat(path, name);
    for (p = path + 3; *p; ++p)
    {
        WCHAR saved;
        if (*p != '\\') continue;
        saved = *p; *p = 0;
        attr = GetFileAttributesW(path);
        if (attr == INVALID_FILE_ATTRIBUTES && mkdirs && (size_t)(p - path) >= root)
        {
            if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            { DWORD error = GetLastError(); *p = saved; return error; }
            attr = GetFileAttributesW(path);
        }
        *p = saved;
        if (attr == INVALID_FILE_ATTRIBUTES) return ERROR_PATH_NOT_FOUND;
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY) || (attr & FILE_ATTRIBUTE_REPARSE_POINT))
            return ERROR_ACCESS_DENIED;
    }
    attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)
             ? ERROR_ACCESS_DENIED : 0;
}

static BOOL HexDigest(WINPR_DIGEST_CTX *hash, char *hex)
{
    BYTE bytes[32]; unsigned i;
    if (!winpr_Digest_Final(hash, bytes, sizeof(bytes))) return FALSE;
    for (i = 0; i < 32; ++i) sprintf(hex + i * 2, "%02x", bytes[i]);
    return TRUE;
}

static DWORD HashLocal(RDP_TRANSFER *t, HANDLE file, UINT64 *size, char *hex)
{
    BYTE *buffer = malloc(CHUNK);
    WINPR_DIGEST_CTX *hash = winpr_Digest_New();
    DWORD status = ERROR_GEN_FAILURE, count;
    *size = 0;
    if (!buffer || !hash || !winpr_Digest_Init(hash, WINPR_MD_SHA256)) goto done;
    while (!Stopping(t))
    {
        if (!ReadFile(file, buffer, CHUNK, &count, NULL)) { status = GetLastError(); goto done; }
        if (!count) { status = HexDigest(hash, hex) ? 0 : ERROR_GEN_FAILURE; goto done; }
        if (!winpr_Digest_Update(hash, buffer, count)) goto done;
        *size += count;
    }
    status = ERROR_OPERATION_ABORTED;
done:
    winpr_Digest_Free(hash); free(buffer); return status;
}

static DWORD PutFile(RDP_TRANSFER *t, const char *source, const char *destination,
                     UINT64 expectedSize, const char *expectedHash, const char *job, unsigned index,
                     UINT64 *copied, char *actualHash, BOOL *skipped)
{
    WCHAR path[MAX_PATH], temp[MAX_PATH], sourceCheck[MAX_PATH], wideJob[33];
    HANDLE local = INVALID_HANDLE_VALUE;
    UINT32 remote = 0;
    BOOL remoteOpen = FALSE, owned = FALSE;
    WINPR_DIGEST_CTX *hash = NULL;
    DWORD status, written, closeStatus;
    *copied = 0; *skipped = FALSE;
    if (!RelativePath(source, sourceCheck)) return ERROR_INVALID_NAME;
    if ((status = LocalPath(t, destination, path, TRUE))) return status;
    local = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (local != INVALID_HANDLE_VALUE)
    {
        status = HashLocal(t, local, copied, actualHash); CloseHandle(local); local = INVALID_HANDLE_VALUE;
        if (!status && *copied == expectedSize && !_stricmp(actualHash, expectedHash))
        { *skipped = TRUE; return 0; }
    }
    *copied = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, job, -1, wideJob, 33) ||
        wcslen(path) + 56 >= MAX_PATH ||
        swprintf(temp, MAX_PATH, L"%ls.rdp-%ls-%u.part", path, wideJob, index) < 0)
        return ERROR_FILENAME_EXCED_RANGE;
    status = RemoteOpen(t, source, FALSE, &remote);
    if (status) return status;
    remoteOpen = TRUE;
    /* CREATE_NEW never removes another job's partial data. */
    local = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (local == INVALID_HANDLE_VALUE) { status = GetLastError(); goto done; }
    owned = TRUE;
    hash = winpr_Digest_New();
    if (!hash || !winpr_Digest_Init(hash, WINPR_MD_SHA256)) { status = ERROR_GEN_FAILURE; goto done; }
    while (!Stopping(t))
    {
        status = RemoteRead(t, remote, *copied, CHUNK);
        if (status == (DWORD)STATUS_END_OF_FILE) { status = 0; break; }
        if (status || !t->Length)
        { t->Log("File transfer read ended: status=%08lx length=%lu offset=%llu", status, (ULONG)t->Length, *copied); break; }
        if (*copied > expectedSize || t->Length > expectedSize - *copied)
        { status = ERROR_INVALID_DATA; goto done; }
        if (!WriteFile(local, t->Data, t->Length, &written, NULL) || written != t->Length)
        { t->Log("File transfer local write failed: error=%lu written=%lu offset=%llu", GetLastError(), written, *copied);
          status = ERROR_WRITE_FAULT; goto done; }
        if (!winpr_Digest_Update(hash, t->Data, t->Length)) { status = ERROR_GEN_FAILURE; goto done; }
        *copied += t->Length;
    }
    if (Stopping(t)) status = ERROR_OPERATION_ABORTED;
    if (status) goto done;
    if (*copied != expectedSize || !HexDigest(hash, actualHash) || _stricmp(actualHash, expectedHash))
    { status = ERROR_CRC; goto done; }
    if (!FlushFileBuffers(local)) { status = GetLastError(); goto done; }
    if (!CloseHandle(local)) { local = INVALID_HANDLE_VALUE; status = GetLastError(); goto done; }
    local = INVALID_HANDLE_VALUE;
    status = RemoteClose(t, remote); remoteOpen = FALSE;
    if (status) goto done;
    /* Recheck the destination before publishing the verified file. */
    if ((status = LocalPath(t, destination, path, FALSE))) goto done;
    if (!MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    { status = GetLastError(); goto done; }
    owned = FALSE;
done:
    if (remoteOpen)
    { closeStatus = RemoteClose(t, remote); if (!status) status = closeStatus; }
    if (local != INVALID_HANDLE_VALUE) CloseHandle(local);
    if (owned) DeleteFileW(temp);
    winpr_Digest_Free(hash);
    return status;
}

static DWORD GetFile(RDP_TRANSFER *t, const char *source, const char *destination,
                     UINT64 *copied, char *actualHash)
{
    WCHAR path[MAX_PATH], check[MAX_PATH];
    HANDLE local;
    UINT32 remote = 0;
    DWORD status, count, closeStatus;
    WINPR_DIGEST_CTX *hash;
    BYTE *buffer;
    *copied = 0;
    if (!RelativePath(destination, check)) return ERROR_INVALID_NAME;
    if ((status = LocalPath(t, source, path, FALSE))) return status;
    local = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (local == INVALID_HANDLE_VALUE) return GetLastError();
    status = RemoteOpen(t, destination, TRUE, &remote);
    if (status) { CloseHandle(local); return status; }
    buffer = malloc(CHUNK); hash = winpr_Digest_New();
    if (!buffer || !hash || !winpr_Digest_Init(hash, WINPR_MD_SHA256))
    { status = ERROR_GEN_FAILURE; goto done; }
    while (!Stopping(t))
    {
        if (!ReadFile(local, buffer, CHUNK, &count, NULL)) { status = GetLastError(); break; }
        if (!count) break;
        status = RemoteWrite(t, remote, *copied, buffer, count);
        if (status) break;
        if (!winpr_Digest_Update(hash, buffer, count)) { status = ERROR_GEN_FAILURE; break; }
        *copied += count;
    }
    if (Stopping(t)) status = ERROR_OPERATION_ABORTED;
    if (!status && !HexDigest(hash, actualHash)) status = ERROR_GEN_FAILURE;
done:
    closeStatus = RemoteClose(t, remote); if (!status) status = closeStatus;
    CloseHandle(local); free(buffer); winpr_Digest_Free(hash);
    return status;
}

static DWORD ReadRequest(RDP_TRANSFER *t, char **request)
{
    UINT32 file;
    DWORD status, closeStatus;
    size_t size = 0;
    char *buffer = NULL, *grown;
    *request = NULL;
    if ((status = RemoteOpen(t, "request.txt", FALSE, &file))) return status;
    while (!Stopping(t))
    {
        status = RemoteRead(t, file, size, CHUNK);
        if (status == (DWORD)STATUS_END_OF_FILE) { status = 0; break; }
        if (status || !t->Length) break;
        if (size + t->Length > MANIFEST_LIMIT || memchr(t->Data, 0, t->Length))
        { status = ERROR_INVALID_DATA; break; }
        grown = realloc(buffer, size + t->Length + 1);
        if (!grown) { status = ERROR_NOT_ENOUGH_MEMORY; break; }
        buffer = grown; memcpy(buffer + size, t->Data, t->Length); size += t->Length; buffer[size] = 0;
    }
    closeStatus = RemoteClose(t, file); if (!status) status = closeStatus;
    if (!status && (!size || buffer[size - 1] != '\n')) status = ERROR_INVALID_DATA;
    if (status) free(buffer); else *request = buffer;
    return status;
}

static BOOL Hex(const char *s, size_t length)
{
    size_t i;
    if (strlen(s) != length) return FALSE;
    for (i = 0; i < length; ++i) if (!isxdigit((unsigned char)s[i])) return FALSE;
    return TRUE;
}

static void RunRequest(RDP_TRANSFER *t, char *request)
{
    char *line = strchr(request, '\n'), job[33], *results;
    size_t used = 0;
    unsigned index = 0;
    UINT32 resultFile;
    DWORD status;
    char resultPath[64];
    if (!line) return;
    *line++ = 0;
    if (strncmp(request, "RDPFILES1\t", 10) || !Hex(request + 10, 32))
    { t->Log("Rejected invalid file transfer manifest header"); return; }
    strcpy(job, request + 10);
    if (!strcmp(job, t->LastJob)) return;
    results = calloc(1, MANIFEST_LIMIT);
    if (!results) return;
    used = snprintf(results, MANIFEST_LIMIT, "RDPRESULT1\t%s\n", job);
    t->Log("File transfer job %s started", job);
    while (*line && index < MAX_FILES && !Stopping(t))
    {
        char *fields[5], *next = strchr(line, '\n'), *cursor = line, *end;
        unsigned i;
        UINT64 expected, copied = 0;
        char hash[65] = "-";
        BOOL skipped = FALSE;
        WCHAR local[MAX_PATH];
        if (!next) break;
        *next++ = 0;
        for (i = 0; i < 5; ++i)
        {
            fields[i] = cursor;
            if (i == 4) break;
            cursor = strchr(cursor, '\t');
            if (!cursor) break;
            *cursor++ = 0;
        }
        status = ERROR_INVALID_DATA;
        if (i == 4 && !strchr(fields[4], '\t') && *fields[1] &&
            fields[1][0] != '-' && fields[1][0] != '+')
        {
            errno = 0;
            expected = _strtoui64(fields[1], &end, 10);
            if (!*end && errno != ERANGE && isdigit((unsigned char)fields[1][0]))
            {
                if (!strcmp(fields[0], "PUT") && Hex(fields[2], 64))
                    status = PutFile(t, fields[3], fields[4], expected, fields[2], job, index,
                                     &copied, hash, &skipped);
                else if (!strcmp(fields[0], "GET"))
                    status = GetFile(t, fields[3], fields[4], &copied, hash);
                else if (!strcmp(fields[0], "MKDIR"))
                {
                    status = LocalPath(t, fields[4], local, TRUE);
                    if (!status && !CreateDirectoryW(local, NULL))
                    {
                        status = GetLastError();
                        if (status == ERROR_ALREADY_EXISTS &&
                            (GetFileAttributesW(local) & FILE_ATTRIBUTE_DIRECTORY)) status = 0;
                    }
                }
            }
        }
        used += snprintf(results + used, MANIFEST_LIMIT - used, "%u\t%08lx\t%llu\t%s\t%s\n",
                         index, status, copied, hash, skipped ? "SKIP" : "COPY");
        t->Log("File transfer %s item=%u status=%08lx bytes=%llu%s", job, index, status,
               copied, skipped ? " unchanged" : "");
        ++index; line = next;
    }
    if (*line && !Stopping(t))
        used += snprintf(results + used, MANIFEST_LIMIT - used, "ERROR\ttoo-many-or-incomplete-items\n");
    used += snprintf(results + used, MANIFEST_LIMIT - used, "END\t%s\n", job);
    snprintf(resultPath, sizeof(resultPath), "results/%s.txt", job);
    status = RemoteOpen(t, resultPath, TRUE, &resultFile);
    if (!status)
    {
        size_t offset = 0;
        while (offset < used)
        {
            UINT32 bytes = (UINT32)min(used - offset, CHUNK);
            status = RemoteWrite(t, resultFile, offset, results + offset, bytes);
            if (status) break;
            offset += bytes;
        }
        { DWORD closeStatus = RemoteClose(t, resultFile); if (!status) status = closeStatus; }
    }
    if (!status) strcpy(t->LastJob, job);
    t->Log("File transfer job %s finished: items=%u result-status=%08lx", job, index, status);
    free(results);
}

static DWORD WINAPI Worker(void *parameter)
{
    RDP_TRANSFER *t = parameter;
    HANDLE events[] = { t->Stop, t->Ready };
    if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) return 0;
    while (!Stopping(t))
    {
        char *request;
        DWORD status = ReadRequest(t, &request);
        if (!status) { RunRequest(t, request); free(request); }
        if (WaitForSingleObject(t->Stop, 1000) != WAIT_TIMEOUT) break;
    }
    return 0;
}

RDP_TRANSFER *TransferStart(HANDLE vcm, rdpContext *rdp, const WCHAR *root, TRANSFER_LOG log)
{
    RDP_TRANSFER *t;
    size_t length = wcslen(root);
    if (length < 3 || length >= MAX_PATH - 2 || root[1] != ':' || root[2] != '\\') return NULL;
    t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    wcscpy(t->Root, root);
    if (t->Root[length - 1] != '\\') { t->Root[length] = '\\'; t->Root[length + 1] = 0; }
    t->Log = log;
    t->Stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    t->Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    t->Reply = CreateEventW(NULL, TRUE, FALSE, NULL);
    t->Channel = rdpdr_server_context_new(vcm);
    if (!t->Stop || !t->Ready || !t->Reply || !t->Channel) goto failed;
    t->Channel->data = t; t->Channel->rdpcontext = rdp;
    t->Channel->supported = RDPDR_DTYP_FILESYSTEM;
    t->Channel->OnDriveCreate = DriveAdded; t->Channel->OnDriveDelete = DriveRemoved;
    t->Channel->OnDriveOpenFileComplete = OpenDone; t->Channel->OnDriveReadFileComplete = ReadDone;
    t->Channel->OnDriveWriteFileComplete = WriteDone; t->Channel->OnDriveCloseFileComplete = CloseDone;
    if (t->Channel->Start(t->Channel)) goto failed;
    t->Thread = CreateThread(NULL, 0, Worker, t, 0, NULL);
    if (!t->Thread) goto failed;
    return t;
failed:
    TransferStop(t); return NULL;
}

void TransferStop(RDP_TRANSFER *t)
{
    if (!t) return;
    if (t->Stop) SetEvent(t->Stop);
    if (t->Thread) { WaitForSingleObject(t->Thread, INFINITE); CloseHandle(t->Thread); }
    if (t->Channel) { t->Channel->Stop(t->Channel); rdpdr_server_context_free(t->Channel); }
    if (t->Reply) CloseHandle(t->Reply);
    if (t->Ready) CloseHandle(t->Ready);
    if (t->Stop) CloseHandle(t->Stop);
    free(t);
}
