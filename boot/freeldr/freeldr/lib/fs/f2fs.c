/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     F2FS support for FreeLoader (read-only, boot-stage)
 *
 * This is the glue layer only: it turns FreeLoader's ARC device interface (DEVVTBL) into the
 * three calls the portable reader needs, and implements `f2fs_dev_read` -- the one callback that
 * reader is defined against.  The on-disk format lives entirely in `f2fs_boot.c`, which is a
 * byte-identical copy of `contrib/freeldr-f2fs/f2fs_boot.c` and knows nothing about ARC.
 *
 * What this file does NOT do, deliberately:
 *   * no writes and no seek-to-EOF: a boot loader that reopens a volume for writing is a bug, so
 *     every non-read open is refused with EACCES (the reader itself never writes either);
 *   * no block-device arithmetic of its own: every byte offset comes from the reader, which takes
 *     them from the Rust core's `layout.rs`, and the sector split happens here and only here;
 *   * no counters and no caches: one `struct f2fs_volume` per mounted device, one
 *     `struct f2fs_inode` per open file, both from FreeLoader's heap, nothing global that grows.
 */

#include <freeldr.h>
#include "fs/stat.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(FILESYSTEM);

/* The portable reader.  It includes <stdint.h> and <string.h> and defines the callback below. */
#include "f2fs_boot.h"

/*
 * Printed on mount, before anything is trusted, because the reader's stability contract is
 * "it never breaks silently": whoever is in recovery has to be able to tell which reader they
 * booted.  Bump the reader's own version when `f2fs_boot.c` changes.
 */
#define F2FS_READER_VERSION "1"

#define TAG_F2FS_VOLUME 'VsfF'
#define TAG_F2FS_INODE  'IsfF'
#define TAG_F2FS_FILE   'FsfF'

/* The on-disk block is 4 KiB; an unaligned window needs at most one extra sector on either end. */
#define F2FS_BOUNCE_SIZE (F2FS_BLK + SECTOR_SIZE)

typedef struct _F2FS_FILE_INFO
{
    struct f2fs_volume* Volume;
    struct f2fs_inode*  Inode;      /* ~7.9 KiB: heap, never the 28 KiB boot stack */
    ULONGLONG           Position;
    UCHAR               Attributes;
    ULONG               FileNameLength;
    CHAR                FileName[32];   /* matches FILEINFORMATION::FileName */
} F2FS_FILE_INFO, *PF2FS_FILE_INFO;

/* One volume per ARC device id, keyed exactly as btrfs.c/ext.c key theirs. */
static struct f2fs_volume* F2FsVolumes[MAX_FDS];

/* Sector-unaligned reads pass through here; never used concurrently (FreeLoader is single-threaded). */
static UCHAR F2FsBounce[F2FS_BOUNCE_SIZE];

/* ==========================================================================================
 * The device callback: byte offset -> ARC seek + read
 *
 * FreeLoader's disk driver (arch/i386/hwdisk.c: DiskSeek) REJECTS a byte position that is not a
 * multiple of the sector size, and its DiskRead() reads whole sectors but copies at most the
 * requested length back.  So the rule this function enforces is:
 *
 *   * an aligned offset + any length is one ArcRead (DiskRead already splits at the BIOS's
 *     64 KiB / 127-sector limit, which is where that boundary lives -- see MAX_DISKREADBUFFER_SIZE
 *     in arch/pc/x86common.h);
 *   * an unaligned offset is served a sector at a time through F2FsBounce, so every ArcSeek this
 *     file issues is aligned by construction and no caller can hand the disk driver a bad one.
 *
 * The reader only ever asks for the superblock (1024) and whole 4 KiB blocks, so the second path
 * exists for correctness, not for the current call pattern.
 * ========================================================================================== */

/* Read `len` bytes at the sector-aligned byte `offset`.  0 on success. */
static int
F2FsReadAligned(
    _In_ uint32_t Device,
    _In_ uint64_t Offset,
    _Out_ void* Buffer,
    _In_ uint32_t Length)
{
    LARGE_INTEGER Position;
    ARC_STATUS Status;
    ULONG Count;

    Position.QuadPart = Offset;
    Status = ArcSeek(Device, &Position, SeekAbsolute);
    if (Status != ESUCCESS)
    {
        ERR("F2FS: ArcSeek(%s) failed: %lu\n", "device", Status);
        return -1;
    }

    Status = ArcRead(Device, Buffer, Length, &Count);
    if (Status != ESUCCESS || Count != Length)
    {
        ERR("F2FS: ArcRead(%lu bytes at 0x%llx) failed: status %lu, count %lu\n",
            Length, (unsigned long long)Offset, Status, Count);
        return -1;
    }

    return 0;
}

int
f2fs_dev_read(
    uint32_t device,
    uint64_t byte_offset,
    void* buffer,
    uint32_t length)
{
    PUCHAR Out = (PUCHAR)buffer;

    if (!buffer && length)
        return -1;
    if (length == 0)
        return 0;

    if ((byte_offset & (SECTOR_SIZE - 1)) == 0)
        return F2FsReadAligned(device, byte_offset, buffer, length);

    /* Unaligned head: read the enclosing sectors into the bounce buffer, copy the part we want. */
    while (length != 0)
    {
        uint64_t Aligned = byte_offset & ~((uint64_t)SECTOR_SIZE - 1);
        uint32_t Shift = (uint32_t)(byte_offset - Aligned);
        uint32_t Chunk = SECTOR_SIZE - Shift;

        if (Chunk > length)
            Chunk = length;

        if (F2FsReadAligned(device, Aligned, F2FsBounce, SECTOR_SIZE) != 0)
            return -1;

        RtlCopyMemory(Out, F2FsBounce + Shift, Chunk);
        Out += Chunk;
        byte_offset += Chunk;
        length -= Chunk;
    }

    return 0;
}

/* ==========================================================================================
 * File handles
 * ========================================================================================== */

static VOID
F2FsFreeFile(
    _In_ PF2FS_FILE_INFO File)
{
    if (!File)
        return;
    if (File->Inode)
        FrLdrTempFree(File->Inode, TAG_F2FS_INODE);
    FrLdrTempFree(File, TAG_F2FS_FILE);
}

ARC_STATUS
F2FsClose(ULONG FileId)
{
    PF2FS_FILE_INFO File = FsGetDeviceSpecific(FileId);

    TRACE("F2FS: close file %lu\n", FileId);
    F2FsFreeFile(File);
    FsSetDeviceSpecific(FileId, NULL);
    return ESUCCESS;
}

ARC_STATUS
F2FsGetFileInformation(ULONG FileId, FILEINFORMATION* Information)
{
    PF2FS_FILE_INFO File = FsGetDeviceSpecific(FileId);

    if (!File)
        return EBADF;

    RtlZeroMemory(Information, sizeof(*Information));
    Information->EndingAddress.QuadPart = File->Inode->size;
    Information->CurrentAddress.QuadPart = File->Position;

    Information->Attributes = File->Attributes;

    Information->FileNameLength = min(File->FileNameLength,
                                      sizeof(Information->FileName) - 1);
    RtlCopyMemory(Information->FileName, File->FileName, Information->FileNameLength);
    Information->FileName[Information->FileNameLength] = ANSI_NULL;

    TRACE("F2FS: GetFileInformation(%lu) -> size %llu, pointer 0x%llx\n",
          FileId, (unsigned long long)File->Inode->size,
          (unsigned long long)File->Position);

    return ESUCCESS;
}

ARC_STATUS
F2FsOpen(CHAR* Path, OPENMODE OpenMode, ULONG* FileId)
{
    struct f2fs_volume* Volume;
    struct f2fs_inode* Inode;
    PF2FS_FILE_INFO File;
    ULONG DeviceId;
    ULONG NameLength = 0;
    PCSTR Name;
    PCSTR p;
    int rc;

    TRACE("F2FS: open '%s' (mode %lu)\n", Path, (ULONG)OpenMode);

    if (OpenMode != OpenReadOnly && OpenMode != OpenDirectory)
        return EACCES;

    DeviceId = FsGetDeviceId(*FileId);
    Volume = F2FsVolumes[DeviceId];
    if (!Volume)
    {
        ERR("F2FS: device %lu is not a mounted f2fs volume\n", DeviceId);
        return ENODEV;
    }

    Inode = FrLdrTempAlloc(sizeof(*Inode), TAG_F2FS_INODE);
    if (!Inode)
        return ENOMEM;

    rc = f2fs_lookup(Volume, Path, Inode);
    if (rc != F2FS_OK)
    {
        /*
         * The reader formats the reason into its own diagnostic -- which component failed, how
         * many entries the parent holds -- so print it here rather than inventing a new message:
         * "f2fs: ..." on the serial line is the recoverable form of a failed boot.
         */
        ERR("F2FS: lookup of '%s' failed (%d): %s\n", Path, rc, f2fs_diag(Volume));
        FrLdrTempFree(Inode, TAG_F2FS_INODE);
        return (rc == F2FS_ENOENT) ? ENOENT : EIO;
    }

    /* A folded (case-insensitive) match is a warning, not an error, but it must be visible. */
    if (f2fs_diag(Volume)[0])
        WARN("F2FS: %s\n", f2fs_diag(Volume));

    /* Check that the caller opened what it expected, the way fat.c does. */
    if ((Inode->mode & F2FS_S_IFMT) == F2FS_S_IFDIR)
    {
        if (OpenMode != OpenDirectory)
        {
            FrLdrTempFree(Inode, TAG_F2FS_INODE);
            return EISDIR;
        }
    }
    else if ((Inode->mode & F2FS_S_IFMT) != F2FS_S_IFREG)
    {
        /* A symlink here would need resolution the boot path does not do; refuse by name. */
        WARN("F2FS: '%s' is a %s, not a regular file\n", Path,
             f2fs_file_type_name((Inode->mode & F2FS_S_IFMT) == F2FS_S_IFLNK ? F2FS_FT_SYMLINK :
                                 (Inode->mode & F2FS_S_IFMT) == F2FS_S_IFDIR ? F2FS_FT_DIR :
                                 F2FS_FT_UNKNOWN));
        FrLdrTempFree(Inode, TAG_F2FS_INODE);
        return EISDIR;
    }
    else if (OpenMode != OpenReadOnly)
    {
        FrLdrTempFree(Inode, TAG_F2FS_INODE);
        return ENOTDIR;
    }

    File = FrLdrTempAlloc(sizeof(*File), TAG_F2FS_FILE);
    if (!File)
    {
        FrLdrTempFree(Inode, TAG_F2FS_INODE);
        return ENOMEM;
    }

    RtlZeroMemory(File, sizeof(*File));
    File->Volume = Volume;
    File->Inode = Inode;
    File->Position = 0;

    File->Attributes = 0;
    if ((Inode->mode & F2FS_S_IFMT) == F2FS_S_IFDIR)
        File->Attributes |= DirectoryFile;
    if (!(Inode->mode & (_S_IWUSR | _S_IWGRP | _S_IWOTH)))
        File->Attributes |= ReadOnlyFile;

    /* FILEINFORMATION carries a short name: keep the last path component, as btrfs.c does. */
    Name = Path;
    for (p = Path; *p; ++p)
    {
        if (*p == '\\' || *p == '/')
            Name = p + 1;
    }
    for (p = Name; *p; ++p)
        NameLength++;
    if (NameLength > sizeof(File->FileName))
        NameLength = sizeof(File->FileName);
    RtlCopyMemory(File->FileName, Name, NameLength);
    File->FileNameLength = NameLength;

    FsSetDeviceSpecific(*FileId, File);

    TRACE("F2FS: '%s' ino %lu, size %llu, mode 0x%lx\n", Path, Inode->ino,
          (unsigned long long)Inode->size, Inode->mode);

    return ESUCCESS;
}

ARC_STATUS
F2FsRead(ULONG FileId, VOID* Buffer, ULONG Size, ULONG* BytesRead)
{
    PF2FS_FILE_INFO File = FsGetDeviceSpecific(FileId);
    uint64_t Done = 0;
    uint64_t Want;
    int rc;

    if (!File)
        return EBADF;

    /*
     * `BytesRead` may be NULL: FreeLoader's own callers do that (linuxboot.c reads the whole
     * kernel with ArcRead(..., NULL)), so "the caller does not want the count" is not an error.
     */
    if (BytesRead)
        *BytesRead = 0;

    if (File->Position >= File->Inode->size)
        return ESUCCESS;        /* at or past EOF: short read of zero bytes */

    Want = File->Inode->size - File->Position;
    if (Want > Size)
        Want = Size;

    rc = f2fs_read(File->Volume, File->Inode, File->Position, Buffer, Want, &Done);
    if (rc != F2FS_OK)
    {
        ERR("F2FS: read of file %lu at 0x%llx failed (%d): %s\n", FileId,
            (unsigned long long)File->Position, rc, f2fs_diag(File->Volume));
        return EIO;
    }

    File->Position += Done;
    if (BytesRead)
        *BytesRead = (ULONG)Done;

    TRACE("F2FS: read %lu bytes at 0x%llx\n", (ULONG)Done,
          (unsigned long long)(File->Position - Done));

    return ESUCCESS;
}

ARC_STATUS
F2FsSeek(ULONG FileId, LARGE_INTEGER* Position, SEEKMODE SeekMode)
{
    PF2FS_FILE_INFO File = FsGetDeviceSpecific(FileId);
    LARGE_INTEGER NewPosition = *Position;

    if (!File)
        return EBADF;

    switch (SeekMode)
    {
        case SeekAbsolute:
            break;
        case SeekRelative:
            NewPosition.QuadPart += (ULONGLONG)File->Position;
            break;
        default:
            ASSERT(FALSE);
            return EINVAL;
    }

    if (NewPosition.QuadPart < 0)
        return EINVAL;

    /* Same rule as fat.c: a seek lands inside the file, never at EOF. */
    if ((ULONGLONG)NewPosition.QuadPart >= File->Inode->size)
        return EINVAL;

    File->Position = (ULONGLONG)NewPosition.QuadPart;
    return ESUCCESS;
}

/**
 * @brief
 * Returns the size of the F2FS volume laid on the storage media device opened via @p DeviceId.
 **/
ULONGLONG
F2FsGetVolumeSize(
    _In_ ULONG DeviceId)
{
    struct f2fs_volume* Volume = F2FsVolumes[DeviceId];

    ASSERT(Volume);
    return (ULONGLONG)Volume->block_count * F2FS_BLK;
}

/* ==========================================================================================
 * Mount probe
 * ========================================================================================== */

const DEVVTBL F2FsFuncTable =
{
    F2FsClose,
    F2FsGetFileInformation,
    F2FsOpen,
    F2FsRead,
    F2FsSeek,
    L"f2fs",
};

const DEVVTBL*
F2FsMount(
    ULONG DeviceId)
{
    struct f2fs_volume* Volume;
    int rc;

    TRACE("F2FS: probe device %lu (reader %s)\n", DeviceId, F2FS_READER_VERSION);

    Volume = FrLdrTempAlloc(sizeof(*Volume), TAG_F2FS_VOLUME);
    if (!Volume)
        return NULL;

    /*
     * The probe reads the superblock, both checkpoint pack candidates (checksum verified), the
     * NAT journal and the NAT bitmap.  It writes nothing but `Volume`, and it does not consult
     * the free-block counters, so a volume that needs repair still mounts here.
     */
    rc = f2fs_probe(DeviceId, Volume);
    if (rc != F2FS_OK)
    {
        /*
         * Copy the diagnostic BEFORE freeing: the reader keeps its message inside the volume
         * structure, so reading it afterwards prints freed memory.  Found by the refusal journey --
         * every other journey takes the success path, which is exactly why this survived.
         */
        char diag[F2FS_DIAG_LEN];
        memcpy(diag, f2fs_diag(Volume), sizeof(diag));
        diag[sizeof(diag) - 1] = '\0';
        FrLdrTempFree(Volume, TAG_F2FS_VOLUME);
        /* Not F2FS at all is the common case (every other probe returns NULL quietly). */
        if (rc == -2)
            return NULL;
        ERR("F2FS: device %lu looks like f2fs but the probe failed (%d): %s\n",
            DeviceId, rc, diag);
        /*
         * A refusal is the one message a user *must* act on, so it goes in a modal box rather than a TUI
         * line.  Two reasons, both learned from looking at screenshots of this loader rather than from
         * reading it: a TUI line is cleared by whatever draws next (the modal does not cover the status line,
         * but the body is repainted), and FreeLoader's own modal -- "Linux kernel '...' not found" -- is
         * still legible in a release build when nothing else is.  The contract asks for "a nameable reason";
         * a reason the user cannot see is not one.
         */
        /* No prefix here: `diag` already begins with "f2fs:", and the first attempt rendered
         * "f2fs: f2fs: unsupported feature bits ..." -- a doubling that only a screenshot showed. */
        UiMessageBox("%s", diag);
        return NULL;
    }

    F2FsVolumes[DeviceId] = Volume;

    /*
     * **Say it on the screen as well.**  `debug.h` is one `#if DBG ... #endif` block, so `TRACE`, `WARN`,
     * `ERR` *and* `DbgPrint` are all compiled out of a release loader -- which left this driver unable to
     * tell a user *anything* in a release build, including that it had refused a volume.  `printf` is
     * `TuiPrintf` outside that block, so it survives.  One line at mount, one on the refusal path above, and
     * a release boot is diagnosable again.
     */
    printf("f2fs: mounted device %lu, root ino %lu\n",
           (unsigned long)DeviceId, (unsigned long)Volume->root_ino);

    TRACE("F2FS: mounted device %lu, reader %s: %llu blocks of %lu, %lu blocks/seg, "
          "cp %lu, nat %lu, main %lu, root ino %lu\n",
          DeviceId, F2FS_READER_VERSION,
          (unsigned long long)Volume->block_count, F2FS_BLK,
          Volume->blocks_per_seg, Volume->cp_blkaddr, Volume->nat_blkaddr,
          Volume->main_blkaddr, Volume->root_ino);

    return &F2FsFuncTable;
}
