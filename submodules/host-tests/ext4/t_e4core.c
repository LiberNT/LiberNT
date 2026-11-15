/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runs the ext4 driver's core on an image file: single commands, and a random run of creates, writes,
 *              truncates, renames, links, deletes and attribute changes checked against a model and by e2fsck
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define _GNU_SOURCE
#include "e4core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>

static int DevFd = -1;
static long FailAfterWrites = -1;
static long WriteCount;

void ext4_port_dbg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void ext4_port_assert_fail(const char *file, int line)
{
    fprintf(stderr, "ASSERT %s:%d\n", file, line);
    abort();
}

static int DevOpen(struct ext4_blockdev *b) { (void)b; return EOK; }
static int DevClose(struct ext4_blockdev *b) { (void)b; return EOK; }
static int DevRead(struct ext4_blockdev *b, void *buf, uint64_t id, uint32_t cnt)
{
    (void)b;
    if (pread(DevFd, buf, (size_t)cnt * 512, (off_t)id * 512) != (ssize_t)((size_t)cnt * 512))
        return EIO;
    return EOK;
}
static int DevWrite(struct ext4_blockdev *b, const void *buf, uint64_t id, uint32_t cnt)
{
    (void)b;
    if (FailAfterWrites >= 0 && WriteCount >= FailAfterWrites)
    {
        /* simulated power cut: the process dies with the image as it is */
        _exit(77);
    }
    WriteCount++;
    if (pwrite(DevFd, buf, (size_t)cnt * 512, (off_t)id * 512) != (ssize_t)((size_t)cnt * 512))
        return EIO;
    return EOK;
}

static uint8_t DevBuf[512];
static struct ext4_blockdev_iface DevIf = {
    .open = DevOpen, .bread = DevRead, .bwrite = DevWrite, .close = DevClose,
    .ph_bsize = 512, .ph_bbuf = DevBuf,
};

static void NowCb(E4_VOLUME *v, E4_TIME *t)
{
    struct timespec ts;
    (void)v;
    clock_gettime(CLOCK_REALTIME, &ts);
    t->Seconds = ts.tv_sec;
    t->Nanoseconds = (uint32_t)ts.tv_nsec;
}

static E4_VOLUME V;
static uint64_t ShrinkTo;

static int MountImage(const char *path, int ro)
{
    struct stat st;
    DevFd = open(path, ro ? O_RDONLY : O_RDWR);
    if (DevFd < 0) { perror(path); return EIO; }
    fstat(DevFd, &st);
    memset(&V, 0, sizeof(V));
    DevIf.ph_bcnt = st.st_size / 512;
    DevIf.ph_refctr = 0;
    V.Device.bdif = &DevIf;
    V.Device.part_offset = 0;
    V.Device.part_size = st.st_size;
    V.Now = NowCb;
    return E4Mount(&V, ro, 256);
}

static int UnmountImage(void)
{
    int r = E4Unmount(&V);
    if (ShrinkTo != 0 && ftruncate(DevFd, (off_t)ShrinkTo) != 0) r = EIO;
    ShrinkTo = 0;
    fsync(DevFd);
    close(DevFd);
    DevFd = -1;
    return r;
}

static int Resolve(const char *path, uint32_t *ino, uint32_t *parent, const char **leaf)
{
    uint32_t cur = E4_ROOT_INODE, par = E4_ROOT_INODE;
    const char *p = path;
    const char *last = "";
    while (*p == '/') p++;
    while (*p)
    {
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        uint32_t next;
        int r;
        const char *rest = p + n;
        while (*rest == '/') rest++;
        last = p;
        par = cur;
        r = E4Lookup(&V, cur, p, (uint32_t)n, &next, NULL);
        if (r != EOK)
        {
            if (parent) *parent = par;
            if (leaf) *leaf = last;
            if (*rest) return ENOTDIR;
            return r;
        }
        cur = next;
        p = rest;
    }
    if (ino) *ino = cur;
    if (parent) *parent = par;
    if (leaf) *leaf = last;
    return EOK;
}

static uint32_t LeafLen(const char *leaf)
{
    const char *e = strchr(leaf, '/');
    return (uint32_t)(e ? (size_t)(e - leaf) : strlen(leaf));
}

static int FileRead(uint32_t ino, uint64_t off, void *buf, uint32_t len)
{
    uint32_t bs = V.BlockSize;
    uint8_t *out = buf;
    while (len)
    {
        uint64_t blk = off / bs, phys;
        uint32_t in = (uint32_t)(off % bs), mapped, chunk;
        int r = E4Map(&V, ino, blk, (len + in + bs - 1) / bs, false, &phys, &mapped, NULL);
        if (r != EOK) return r;
        chunk = mapped * bs - in;
        if (chunk > len) chunk = len;
        if (phys == 0)
            memset(out, 0, chunk);
        else
        {
            r = ext4_block_readbytes(&V.Device, phys * bs + in, out, chunk);
            if (r != EOK) return r;
        }
        out += chunk; off += chunk; len -= chunk;
    }
    return EOK;
}

static int FileWrite(uint32_t ino, uint64_t off, const void *buf, uint32_t len)
{
    uint32_t bs = V.BlockSize;
    const uint8_t *in_buf = buf;
    E4_ATTR a;
    uint64_t end = off + len;
    int r = E4GetAttr(&V, ino, &a);
    if (r != EOK) return r;
    while (len)
    {
        uint64_t blk = off / bs, phys;
        uint32_t in = (uint32_t)(off % bs), mapped, chunk;
        bool fresh;
        /* as the driver does: the blocks and the size that covers them in one transaction */
        r = E4Begin(&V);
        if (r != EOK) return r;
        r = E4Map(&V, ino, blk, (len + in + bs - 1) / bs, true, &phys, &mapped, &fresh);
        if (r == EOK && fresh)
        {
            uint64_t reach = (blk + mapped) * (uint64_t)bs;
            r = E4GrowSize(&V, ino, reach < end ? reach : end);
        }
        r = E4End(&V, r);
        if (r != EOK) return r;
        chunk = mapped * bs - in;
        if (chunk > len) chunk = len;
        if (fresh)
        {
            /* zero what the write does not cover in the new blocks */
            static uint8_t zero[65536];
            if (in)
            {
                r = ext4_block_writebytes(&V.Device, phys * bs, zero, in);
                if (r != EOK) return r;
            }
            if ((in + chunk) % bs)
            {
                uint32_t tail = bs - ((in + chunk) % bs);
                r = ext4_block_writebytes(&V.Device, phys * bs + in + chunk, zero, tail);
                if (r != EOK) return r;
            }
        }
        r = ext4_block_writebytes(&V.Device, phys * bs + in, in_buf, chunk);
        if (r != EOK) return r;
        in_buf += chunk; off += chunk; len -= chunk;
    }
    if (end > a.Size)
        return E4SetSize(&V, ino, end);
    return EOK;
}

static bool LsCb(void *ctx, const char *name, uint32_t len, uint32_t ino, uint8_t type, uint64_t off)
{
    (void)ctx; (void)off;
    printf("%8u %u %.*s\n", ino, type, (int)len, name);
    return true;
}

static int DoCreate(const char *path, uint32_t mode, uint32_t uid, uint32_t gid, uint32_t maj, uint32_t min, uint32_t *out)
{
    uint32_t parent, ino;
    const char *leaf;
    E4_ATTR a;
    int r = Resolve(path, &ino, &parent, &leaf);
    if (r == EOK) return EEXIST;
    if (r != ENOENT) return r;
    memset(&a, 0, sizeof(a));
    a.Mode = mode; a.Uid = uid; a.Gid = gid; a.DeviceMajor = maj; a.DeviceMinor = min;
    r = E4Create(&V, parent, leaf, LeafLen(leaf), &a, &ino);
    if (r == EOK && out) *out = ino;
    return r;
}

static int DoRemove(const char *path)
{
    uint32_t parent, ino;
    const char *leaf;
    bool last;
    int r = Resolve(path, &ino, &parent, &leaf);
    if (r != EOK) return r;
    r = E4Unlink(&V, parent, leaf, LeafLen(leaf), ino, &last);
    if (r == EOK && last) r = E4Release(&V, ino);
    return r;
}

static int DoRename(const char *from, const char *to)
{
    uint32_t op, np, ino, victim = 0;
    const char *ol, *nl;
    bool last = false;
    int r = Resolve(from, &ino, &op, &ol);
    if (r != EOK) return r;
    r = Resolve(to, &victim, &np, &nl);
    if (r != EOK && r != ENOENT) return r;
    if (r == ENOENT) victim = 0;
    if (victim == ino) return EOK;
    r = E4Rename(&V, op, ol, LeafLen(ol), np, nl, LeafLen(nl), ino, victim, &last);
    if (r == EOK && last) r = E4Release(&V, victim);
    return r;
}

/* ---------------- stress ---------------- */

#define MAXN 400
typedef struct { char path[256]; int used; int isdir; int islink; uint8_t *data; uint32_t size; uint32_t mode, uid, gid; int linkof; char target[120]; } NODE;
static NODE N[MAXN];
static uint64_t Rng = 88172645463325252ULL;
static uint32_t R(void) { Rng ^= Rng << 13; Rng ^= Rng >> 7; Rng ^= Rng << 17; return (uint32_t)(Rng >> 11); }

static int PickUsed(int wantdir, int wantfile)
{
    int tries;
    for (tries = 0; tries < 200; tries++)
    {
        int i = R() % MAXN;
        if (!N[i].used) continue;
        if (wantdir && !N[i].isdir) continue;
        if (wantfile && (N[i].isdir || N[i].islink)) continue;
        return i;
    }
    return -1;
}
static int PickFree(void)
{
    int i;
    for (i = 0; i < MAXN; i++) if (!N[i].used) return i;
    return -1;
}
static void NewPath(char *out, size_t n)
{
    int d = PickUsed(1, 0);
    static const char *long_tail = "_a_rather_long_component_name_to_fill_directory_blocks_faster_and_split_them";
    snprintf(out, n, "%s/n%u%s", d < 0 ? "" : N[d].path, R() % 100000, (R() % 3) ? "" : long_tail);
}
static int HasChild(int d)
{
    size_t l = strlen(N[d].path);
    int i;
    for (i = 0; i < MAXN; i++)
        if (N[i].used && i != d && !strncmp(N[i].path, N[d].path, l) && N[i].path[l] == '/') return 1;
    return 0;
}
static int Exists(const char *p)
{
    int i;
    for (i = 0; i < MAXN; i++) if (N[i].used && !strcmp(N[i].path, p)) return i;
    return -1;
}
/* hard links share one data buffer: find every node sharing it */
static void SetShared(int i, uint8_t *data, uint32_t size)
{
    uint8_t *old = N[i].data;
    int k;
    for (k = 0; k < MAXN; k++)
        if (N[k].used && k != i && !N[k].isdir && !N[k].islink && N[k].data == old && old != NULL)
        { N[k].data = data; N[k].size = size; }
    N[i].data = data; N[i].size = size;
}
static int Shared(int i)
{
    int k, c = 0;
    if (!N[i].data) return 0;
    for (k = 0; k < MAXN; k++) if (N[k].used && k != i && N[k].data == N[i].data) c++;
    return c;
}

#define CHECK(r, what) do { if ((r) != EOK) { fprintf(stderr, "FAIL %s r=%d op=%d path=%s\n", what, (r), op, path); return 1; } } while (0)

static int VerifyAll(void)
{
    int i;
    for (i = 0; i < MAXN; i++)
    {
        uint32_t ino;
        E4_ATTR a;
        int r;
        if (!N[i].used) continue;
        r = Resolve(N[i].path, &ino, NULL, NULL);
        if (r != EOK) { fprintf(stderr, "VERIFY missing %s r=%d\n", N[i].path, r); return 1; }
        r = E4GetAttr(&V, ino, &a);
        if (r != EOK) return 1;
        if ((a.Mode & 07777) != N[i].mode || a.Uid != N[i].uid || a.Gid != N[i].gid)
        { fprintf(stderr, "VERIFY attr %s mode %o/%o uid %u/%u gid %u/%u\n", N[i].path, a.Mode & 07777, N[i].mode, a.Uid, N[i].uid, a.Gid, N[i].gid); return 1; }
        if (N[i].islink)
        {
            char buf[4096]; uint32_t len;
            r = E4ReadLink(&V, ino, buf, sizeof(buf), &len);
            if (r != EOK || len != strlen(N[i].target) || memcmp(buf, N[i].target, len))
            { fprintf(stderr, "VERIFY link %s r=%d\n", N[i].path, r); return 1; }
        }
        else if (!N[i].isdir)
        {
            uint8_t *buf;
            if (a.Size != N[i].size) { fprintf(stderr, "VERIFY size %s %llu want %u\n", N[i].path, (unsigned long long)a.Size, N[i].size); return 1; }
            buf = malloc(N[i].size + 1);
            r = FileRead(ino, 0, buf, N[i].size);
            if (r != EOK || memcmp(buf, N[i].data, N[i].size))
            {
                uint32_t k; for (k = 0; k < N[i].size && buf[k] == N[i].data[k]; k++);
                fprintf(stderr, "VERIFY data %s r=%d first diff at %u of %u\n", N[i].path, r, k, N[i].size); free(buf); return 1;
            }
            free(buf);
        }
    }
    return 0;
}

static int Stress(const char *image, uint64_t seed, int count, int remount_every)
{
    int step;
    char cmd[512];
    Rng ^= seed * 0x9E3779B97F4A7C15ULL;
    for (step = 0; step < count; step++)
    {
        int op = R() % 20;
        char path[256] = "";
        int i, r;
        if (op <= 3)
        {   /* create file with data */
            uint32_t ino, size = (R() % 4 == 0) ? R() % 900000 : R() % 20000;
            i = PickFree(); if (i < 0) continue;
            NewPath(path, sizeof(path)); if (Exists(path) >= 0) continue;
            memset(&N[i], 0, sizeof(N[i]));
            N[i].mode = R() & 0777; N[i].uid = R() % 70000; N[i].gid = R() % 70000;
            r = DoCreate(path, E4_S_IFREG | N[i].mode, N[i].uid, N[i].gid, 0, 0, &ino); CHECK(r, "create");
            N[i].data = malloc(size + 1); N[i].size = size;
            { uint32_t k; for (k = 0; k < size; k++) N[i].data[k] = (uint8_t)R(); }
            r = FileWrite(ino, 0, N[i].data, size); CHECK(r, "write");
            strcpy(N[i].path, path); N[i].used = 1;
        }
        else if (op <= 5)
        {   /* mkdir */
            i = PickFree(); if (i < 0) continue;
            NewPath(path, sizeof(path)); if (Exists(path) >= 0 || strlen(path) > 150) continue;
            memset(&N[i], 0, sizeof(N[i]));
            N[i].mode = 0755; N[i].uid = R() % 3000; N[i].gid = R() % 3000;
            r = DoCreate(path, E4_S_IFDIR | N[i].mode, N[i].uid, N[i].gid, 0, 0, NULL); CHECK(r, "mkdir");
            strcpy(N[i].path, path); N[i].used = 1; N[i].isdir = 1;
        }
        else if (op <= 7)
        {   /* overwrite / extend a region */
            uint32_t ino, off, len, end; uint8_t *nd;
            i = PickUsed(0, 1); if (i < 0) continue;
            strcpy(path, N[i].path);
            off = R() % (N[i].size + 30000); len = R() % 60000 + 1; end = off + len;
            r = Resolve(path, &ino, NULL, NULL); CHECK(r, "resolve");
            nd = N[i].data;
            if (end > N[i].size) { nd = realloc(N[i].data, end + 1); if (off > N[i].size) memset(nd + N[i].size, 0, off - N[i].size); }
            { uint32_t k; for (k = off; k < end; k++) nd[k] = (uint8_t)R(); }
            /* a hole between the old end and the write start must read as zero: extend first */
            if (off > N[i].size) { r = E4SetSize(&V, ino, off); CHECK(r, "extend"); }
            r = FileWrite(ino, off, nd + off, len); CHECK(r, "rewrite");
            SetShared(i, nd, end > N[i].size ? end : N[i].size);
        }
        else if (op <= 9)
        {   /* truncate */
            uint32_t ino, size;
            i = PickUsed(0, 1); if (i < 0) continue;
            strcpy(path, N[i].path);
            size = (R() % 3 == 0) ? 0 : R() % (N[i].size + 5000);
            r = Resolve(path, &ino, NULL, NULL); CHECK(r, "resolve");
            r = E4SetSize(&V, ino, size); CHECK(r, "truncate");
            if (size > N[i].size)
            {
                uint8_t *nd = realloc(N[i].data, size + 1);
                memset(nd + N[i].size, 0, size - N[i].size);
                /* the tail of the last old block must have been zeroed by the writer */
                SetShared(i, nd, size);
            }
            else SetShared(i, N[i].data, size);
        }
        else if (op <= 11)
        {   /* delete file, link or empty dir */
            i = PickUsed(0, 0); if (i < 0) continue;
            if (N[i].isdir && HasChild(i)) continue;
            strcpy(path, N[i].path);
            r = DoRemove(path); CHECK(r, "remove");
            if (!N[i].isdir && !N[i].islink && !Shared(i)) free(N[i].data);
            N[i].used = 0;
        }
        else if (op <= 14)
        {   /* rename, sometimes over an existing file */
            char to[256]; int j = -1;
            size_t l;
            i = PickUsed(0, 0); if (i < 0) continue;
            strcpy(path, N[i].path);
            if (R() % 4 == 0)
            {
                j = PickUsed(0, 0);
                if (j < 0 || j == i || N[j].isdir != N[i].isdir || (N[j].isdir && HasChild(j))) continue;
                if (!N[i].isdir && !N[i].islink && !N[j].islink && N[j].data == N[i].data && N[i].data) continue;
                strcpy(to, N[j].path);
            }
            else { NewPath(to, sizeof(to)); if (Exists(to) >= 0) continue; }
            l = strlen(path);
            if (!strncmp(to, path, l) && (to[l] == '/' || to[l] == 0)) continue;
            if (N[i].isdir)
            {
                int k, toolong = 0;
                for (k = 0; k < MAXN; k++)
                    if (N[k].used && !strncmp(N[k].path, path, l) && N[k].path[l] == '/' && strlen(to) + strlen(N[k].path + l) > 180) toolong = 1;
                if (toolong || strlen(to) > 150) continue;
            }
            if (j >= 0 && N[j].isdir && !strncmp(path, N[j].path, strlen(N[j].path)) && path[strlen(N[j].path)] == '/') continue;
            r = DoRename(path, to); CHECK(r, "rename");
            if (j >= 0) { if (!N[j].isdir && !N[j].islink && !Shared(j)) free(N[j].data); N[j].used = 0; }
            if (N[i].isdir)
            {
                int k;
                for (k = 0; k < MAXN; k++)
                    if (N[k].used && k != i && !strncmp(N[k].path, path, l) && N[k].path[l] == '/')
                    { char t[512]; snprintf(t, sizeof(t), "%s%s", to, N[k].path + l); if (strlen(t) >= sizeof(N[k].path)) { fprintf(stderr, "path too long\n"); return 1; } strcpy(N[k].path, t); }
            }
            strcpy(N[i].path, to);
        }
        else if (op == 15)
        {   /* hard link */
            int j; uint32_t ino, parent; const char *leaf;
            i = PickUsed(0, 1); j = PickFree(); if (i < 0 || j < 0) continue;
            NewPath(path, sizeof(path)); if (Exists(path) >= 0) continue;
            r = Resolve(N[i].path, &ino, NULL, NULL); CHECK(r, "resolve");
            r = Resolve(path, NULL, &parent, &leaf); if (r != ENOENT) CHECK(r ? r : EEXIST, "linkresolve");
            r = E4Link(&V, parent, leaf, LeafLen(leaf), ino); CHECK(r, "link");
            N[j] = N[i]; strcpy(N[j].path, path);
            if (N[i].data == NULL) { N[i].data = malloc(1); N[j].data = N[i].data; }
        }
        else if (op == 16)
        {   /* symlink, short or long */
            uint32_t ino; uint32_t tl = (R() % 2) ? R() % 50 + 1 : R() % 100 + 15, k;
            i = PickFree(); if (i < 0) continue;
            NewPath(path, sizeof(path)); if (Exists(path) >= 0) continue;
            memset(&N[i], 0, sizeof(N[i]));
            for (k = 0; k < tl; k++) N[i].target[k] = 'a' + R() % 26;
            N[i].mode = 0777; N[i].uid = R() % 3000; N[i].gid = N[i].uid;
            r = DoCreate(path, E4_S_IFLNK | 0777, N[i].uid, N[i].gid, 0, 0, &ino); CHECK(r, "symlink");
            r = E4WriteLink(&V, ino, N[i].target, tl); CHECK(r, "writelink");
            strcpy(N[i].path, path); N[i].used = 1; N[i].islink = 1;
        }
        else if (op == 17)
        {   /* chmod / chown: every hard link sees it */
            uint32_t ino; E4_ATTR a; int k;
            i = PickUsed(0, 0); if (i < 0 || N[i].islink) continue;
            strcpy(path, N[i].path);
            memset(&a, 0, sizeof(a));
            a.Mode = R() & 07777; a.Uid = R(); a.Gid = R() % 100000;
            r = Resolve(path, &ino, NULL, NULL); CHECK(r, "resolve");
            r = E4SetAttr(&V, ino, E4_SET_MODE | E4_SET_UID | E4_SET_GID, &a); CHECK(r, "setattr");
            for (k = 0; k < MAXN; k++)
                if (N[k].used && k != i && !N[k].isdir && !N[k].islink && !N[i].isdir && N[k].data == N[i].data && N[i].data)
                { N[k].mode = a.Mode; N[k].uid = a.Uid; N[k].gid = a.Gid; }
            N[i].mode = a.Mode; N[i].uid = a.Uid; N[i].gid = a.Gid;
        }
        else if (op == 18)
        {   /* xattr set / get / remove */
            uint32_t ino, len; char val[300], got[300]; uint32_t vl = R() % 250 + 1, k;
            i = PickUsed(0, 0); if (i < 0 || N[i].islink) continue;
            strcpy(path, N[i].path);
            for (k = 0; k < vl; k++) val[k] = (char)R();
            r = Resolve(path, &ino, NULL, NULL); CHECK(r, "resolve");
            r = E4XattrSet(&V, ino, "user.stress", 11, val, vl); CHECK(r, "xset");
            r = E4XattrGet(&V, ino, "user.stress", 11, got, sizeof(got), &len); CHECK(r, "xget");
            if (len != vl || memcmp(val, got, vl)) { fprintf(stderr, "xattr mismatch\n"); return 1; }
            if (R() % 2) { r = E4XattrRemove(&V, ino, "user.stress", 11); CHECK(r, "xrm"); }
        }
        else
        {   /* device node and fifo */
            uint32_t ino; E4_ATTR a;
            NewPath(path, sizeof(path)); if (Exists(path) >= 0) continue;
            r = DoCreate(path, (R() % 2 ? E4_S_IFCHR : E4_S_IFIFO) | 0600, 0, 0, R() % 5000, R() % 70000, &ino); CHECK(r, "mknod");
            r = E4GetAttr(&V, ino, &a); CHECK(r, "getattr");
            r = DoRemove(path); CHECK(r, "rmnod");
        }

        if (getenv("E4_TRACE")) fprintf(stderr, "step %d op %d path %s\n", step, op, path);
        if (getenv("E4_STOP_AT") && step == atoi(getenv("E4_STOP_AT"))) return 0;
        if (remount_every && (step % remount_every) == remount_every - 1)
        {
            int r2;
            if (VerifyAll()) { fprintf(stderr, "verify failed before remount at step %d\n", step); return 1; }
            r2 = UnmountImage();
            if (r2 != EOK) { fprintf(stderr, "unmount r=%d\n", r2); return 1; }
            snprintf(cmd, sizeof(cmd), "e2fsck -fn %s > e4fsck.%s.out 2>&1", image, getenv("E4_NAME") ? getenv("E4_NAME") : "x");
            if (system(cmd) != 0) { fprintf(stderr, "e2fsck failed at step %d (see the e4fsck output next to the image)\n", step); return 1; }
            r2 = MountImage(image, 0);
            if (r2 != EOK) { fprintf(stderr, "remount r=%d\n", r2); return 1; }
            if (VerifyAll()) { fprintf(stderr, "verify failed after remount at step %d\n", step); return 1; }
        }
    }
    if (VerifyAll()) return 1;
    {
        /* leave a manifest so the kernel's view can be compared too */
        FILE *f = fopen("e4manifest.txt", "w");
        int i;
        for (i = 0; i < MAXN; i++)
            if (N[i].used)
            {
                uint32_t k, sum = 0;
                for (k = 0; k < N[i].size && !N[i].isdir && !N[i].islink; k++) sum = sum * 31 + N[i].data[k];
                fprintf(f, "%s %c %u %u %o %u %u %s\n", N[i].path, N[i].isdir ? 'd' : N[i].islink ? 'l' : 'f', N[i].size, sum, N[i].mode, N[i].uid, N[i].gid, N[i].target);
            }
        fclose(f);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i, r, rc = 0;
    if (argc < 3) { fprintf(stderr, "usage: e4tool IMAGE cmd args... [-- cmd args...]\n"); return 2; }
    if (getenv("E4_DEBUG")) ext4_dmask_set(DEBUG_ALL);
    if (getenv("E4_FAIL_AFTER")) FailAfterWrites = atol(getenv("E4_FAIL_AFTER"));
    r = MountImage(argv[1], getenv("E4_RO") != NULL);
    if (r != EOK) { fprintf(stderr, "mount failed: %d\n", r); return 1; }
    for (i = 2; i < argc && rc == 0; )
    {
        const char *c = argv[i];
        int n = 0;
        while (i + 1 + n < argc && strcmp(argv[i + 1 + n], "--")) n++;
#define A(k) argv[i + 1 + (k)]
        r = EOK;
        if (!strcmp(c, "ls") && n >= 1)
        {
            uint32_t ino; uint64_t off = 0;
            r = Resolve(A(0), &ino, NULL, NULL);
            if (r == EOK) r = E4ReadDir(&V, ino, &off, LsCb, NULL);
        }
        else if (!strcmp(c, "stat") && n >= 1)
        {
            uint32_t ino; E4_ATTR a;
            r = Resolve(A(0), &ino, NULL, NULL);
            if (r == EOK) r = E4GetAttr(&V, ino, &a);
            if (r == EOK) printf("ino %u mode %o uid %u gid %u links %u size %llu sectors %llu mtime %lld.%09u dev %u:%u\n", ino, a.Mode, a.Uid, a.Gid, a.Links,
                                 (unsigned long long)a.Size, (unsigned long long)a.Sectors, (long long)a.Modify.Seconds, a.Modify.Nanoseconds, a.DeviceMajor, a.DeviceMinor);
        }
        else if (!strcmp(c, "cat") && n >= 1)
        {
            uint32_t ino; E4_ATTR a;
            r = Resolve(A(0), &ino, NULL, NULL);
            if (r == EOK) r = E4GetAttr(&V, ino, &a);
            if (r == EOK)
            {
                uint64_t off = 0; static uint8_t buf[1 << 20];
                while (off < a.Size && r == EOK)
                {
                    uint32_t ch = (a.Size - off > sizeof(buf)) ? sizeof(buf) : (uint32_t)(a.Size - off);
                    r = FileRead(ino, off, buf, ch);
                    if (r == EOK) fwrite(buf, 1, ch, stdout);
                    off += ch;
                }
            }
        }
        else if (!strcmp(c, "put") && n >= 2)
        {
            uint32_t ino; FILE *f = fopen(A(0), "rb");
            if (!f) r = ENOENT;
            else
            {
                static uint8_t buf[1 << 20]; uint64_t off = 0; size_t got;
                r = DoCreate(A(1), E4_S_IFREG | 0644, 0, 0, 0, 0, &ino);
                while (r == EOK && (got = fread(buf, 1, sizeof(buf), f)) > 0) { r = FileWrite(ino, off, buf, (uint32_t)got); off += got; }
                fclose(f);
            }
        }
        else if (!strcmp(c, "mkdir") && n >= 1) r = DoCreate(A(0), E4_S_IFDIR | 0755, 0, 0, 0, 0, NULL);
        else if (!strcmp(c, "rm") && n >= 1) r = DoRemove(A(0));
        else if (!strcmp(c, "mv") && n >= 2) r = DoRename(A(0), A(1));
        else if (!strcmp(c, "symlink") && n >= 2)
        {
            uint32_t ino;
            r = DoCreate(A(1), E4_S_IFLNK | 0777, 0, 0, 0, 0, &ino);
            if (r == EOK) r = E4WriteLink(&V, ino, A(0), (uint32_t)strlen(A(0)));
        }
        else if (!strcmp(c, "chown") && n >= 3)
        {
            uint32_t ino; E4_ATTR a; memset(&a, 0, sizeof(a));
            a.Uid = (uint32_t)atol(A(0)); a.Gid = (uint32_t)atol(A(1));
            r = Resolve(A(2), &ino, NULL, NULL);
            if (r == EOK) r = E4SetAttr(&V, ino, E4_SET_UID | E4_SET_GID, &a);
        }
        else if (!strcmp(c, "chmod") && n >= 2)
        {
            uint32_t ino; E4_ATTR a; memset(&a, 0, sizeof(a));
            a.Mode = (uint32_t)strtol(A(0), NULL, 8);
            r = Resolve(A(1), &ino, NULL, NULL);
            if (r == EOK) r = E4SetAttr(&V, ino, E4_SET_MODE, &a);
        }
        else if (!strcmp(c, "truncate") && n >= 2)
        {
            uint32_t ino;
            r = Resolve(A(1), &ino, NULL, NULL);
            if (r == EOK) r = E4SetSize(&V, ino, strtoull(A(0), NULL, 0));
        }
        else if (!strcmp(c, "statfs"))
        {
            E4_STATFS s; E4StatFs(&V, &s);
            printf("bs %u blocks %llu free %llu avail %llu inodes %u free %u label '%s'\n", s.BlockSize, (unsigned long long)s.Blocks,
                   (unsigned long long)s.FreeBlocks, (unsigned long long)s.AvailableBlocks, s.Inodes, s.FreeInodes, s.Label);
        }
        else if (!strcmp(c, "resizeinfo"))
        {
            E4_RESIZE_INFO ri; r = E4ResizeQuery(&V, &ri);
            if (r == EOK) printf("blocks %llu minimum %llu bpg %u bs %u\n", (unsigned long long)ri.Blocks, (unsigned long long)ri.MinimumBlocks, ri.BlocksPerGroup, ri.BlockSize);
        }
        else if (!strcmp(c, "resize") && n >= 1)
        {
            /* "min" shrinks as far as the query allows; the image file is grown first when the size asks for it */
            uint64_t want, reached = 0; E4_RESIZE_INFO ri; struct stat st;
            r = E4ResizeQuery(&V, &ri);
            want = !strcmp(A(0), "min") ? ri.MinimumBlocks : strtoull(A(0), NULL, 0);
            fstat(DevFd, &st);
            if (r == EOK && want * V.BlockSize > (uint64_t)st.st_size)
            {
                if (ftruncate(DevFd, (off_t)(want * V.BlockSize)) != 0) r = EIO;
                DevIf.ph_bcnt = want * V.BlockSize / 512;
                V.Device.part_size = want * V.BlockSize;
                ext4_block_set_lb_size(&V.Device, V.BlockSize);
            }
            if (r == EOK) r = E4Resize(&V, want, &reached);
            if (r == EOK) printf("resized to %llu blocks\n", (unsigned long long)reached);
            if (r == EOK && reached * V.BlockSize < (uint64_t)st.st_size && getenv("E4_TRUNCATE")) ShrinkTo = reached * V.BlockSize;
        }
        else if (!strcmp(c, "verify"))
        {
            /* compare the volume with the manifest a stress run left: path type size sum mode uid gid [target] */
            FILE *f = fopen("e4manifest.txt", "r"); char line[1024]; int bad = 0, cnt = 0;
            if (!f) r = ENOENT;
            while (f && fgets(line, sizeof(line), f))
            {
                char path[512], target[256] = ""; char type; unsigned size, sum, mode, uid, gid; uint32_t ino; E4_ATTR a;
                if (sscanf(line, "%511s %c %u %u %o %u %u %255s", path, &type, &size, &sum, &mode, &uid, &gid, target) < 7) continue;
                cnt++;
                if (Resolve(path, &ino, NULL, NULL) != EOK || E4GetAttr(&V, ino, &a) != EOK) { fprintf(stderr, "VERIFY missing %s\n", path); bad++; continue; }
                if ((a.Mode & 07777) != mode || a.Uid != uid || a.Gid != gid) { fprintf(stderr, "VERIFY attr %s\n", path); bad++; }
                if (type == 'f')
                {
                    uint8_t *buf = malloc(size + 1); uint32_t k, got = 0;
                    if (a.Size != size || FileRead(ino, 0, buf, size) != EOK) { fprintf(stderr, "VERIFY read %s\n", path); bad++; }
                    else { for (k = 0; k < size; k++) got = got * 31 + buf[k]; if (got != sum) { fprintf(stderr, "VERIFY data %s\n", path); bad++; } }
                    free(buf);
                }
                else if (type == 'l')
                {
                    char buf[4096]; uint32_t len = 0;
                    if (E4ReadLink(&V, ino, buf, sizeof(buf), &len) != EOK || len != strlen(target) || memcmp(buf, target, len)) { fprintf(stderr, "VERIFY link %s\n", path); bad++; }
                }
            }
            if (f) fclose(f);
            printf("verified %d entries, %d bad\n", cnt, bad);
            if (bad) rc = 1;
        }
        else if (!strcmp(c, "stress") && n >= 2)
        {
            if (Stress(argv[1], strtoull(A(0), NULL, 0), atoi(A(1)), n >= 3 ? atoi(A(2)) : 0)) rc = 1;
        }
        else { fprintf(stderr, "unknown command %s\n", c); rc = 2; }
        if (r != EOK) { fprintf(stderr, "%s: error %d\n", c, r); rc = 1; }
        i += 1 + n;
        if (i < argc && !strcmp(argv[i], "--")) i++;
    }
    r = UnmountImage();
    if (r != EOK) { fprintf(stderr, "unmount: error %d\n", r); rc = 1; }
    return rc;
}
