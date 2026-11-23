/*
 * PROJECT:     LiberNT NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Interface between the Linux kernel glue and the NTFS core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef NTFSLIB_K_H
#define NTFSLIB_K_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long long nk_u64;
typedef unsigned int nk_u32;

struct nk_volume;

/* Times are NT ticks: 100 ns since 1601. */
struct nk_attr
{
    nk_u64 reference;
    nk_u64 size;
    nk_u64 allocated;
    nk_u64 created;
    nk_u64 accessed;
    nk_u64 written;
    nk_u64 changed;
    nk_u32 links;
    nk_u32 directory;
    /* The data is plain clusters on the device, valid up to this many bytes; an empty file can become so. */
    nk_u32 mapped;
    nk_u32 empty;
    nk_u64 valid;
    /* Sparse or compressed: the stream can hold holes and grow past the volume's size. */
    nk_u32 holes;
    /* POSIX ownership, whole st_mode and device number, for those of them the record stores (NK_HAS_*). */
    nk_u32 has;
    nk_u32 uid;
    nk_u32 gid;
    nk_u32 mode;
    nk_u32 device;
};

#define NK_HAS_UID 1u
#define NK_HAS_GID 2u
#define NK_HAS_MODE 4u
#define NK_HAS_DEVICE 8u

/* What a new file is created with; device counts only with NK_HAS_DEVICE in has. */
struct nk_owner
{
    nk_u32 has;
    nk_u32 uid;
    nk_u32 gid;
    nk_u32 mode;
    nk_u32 device;
};

/* What a listing can tell about a name without reading its record. */
#define NK_NAME_FILE 0
#define NK_NAME_DIRECTORY 1
#define NK_NAME_UNKNOWN 2

struct nk_space
{
    nk_u64 clusters;
    nk_u64 free_clusters;
    nk_u32 cluster_bytes;
};

/* Provided by the kernel glue. */
void* nk_alloc(unsigned long size);
void nk_free(void* memory);
int nk_dev_read(void* device, nk_u64 offset, nk_u32 length, void* buffer);
int nk_dev_write(void* device, nk_u64 offset, nk_u32 length, const void* buffer);
int nk_dev_flush(void* device);
nk_u64 nk_time_now(void);
void nk_note(const char* what, long value);

/* Provided by the core side; every call returns 0 or a negative errno, and runs under the caller's lock. */
int nk_mount(void* device, nk_u32 sector_bytes, int read_only, struct nk_volume** volume, nk_u64* root);
void nk_unmount(struct nk_volume* volume);
void nk_settle(struct nk_volume* volume);
nk_u64 nk_dir_time(struct nk_volume* volume, nk_u64 reference);
int nk_space(struct nk_volume* volume, struct nk_space* space);
int nk_getattr(struct nk_volume* volume, nk_u64 reference, struct nk_attr* attr);
int nk_lookup(struct nk_volume* volume, nk_u64 directory, const char* name, nk_u32 length, struct nk_attr* child);
int nk_create(struct nk_volume* volume, nk_u64 directory, const char* name, nk_u32 length, int is_directory,
              const struct nk_owner* owner, struct nk_attr* child);
int nk_set_owner(struct nk_volume* volume, nk_u64 reference, nk_u32 uid, nk_u32 gid, nk_u32 mode);
/* A further name for an existing file; after says how many it has now. */
int nk_link(struct nk_volume* volume, nk_u64 reference, nk_u64 directory, const char* name, nk_u32 length,
            struct nk_attr* after);
int nk_symlink(struct nk_volume* volume, nk_u64 directory, const char* name, nk_u32 length, const char* target,
               nk_u32 target_length, const struct nk_owner* owner, struct nk_attr* child);
/* The target of a symbolic link as UTF-8, not terminated; returns its length or a negative errno. */
long nk_readlink(struct nk_volume* volume, nk_u64 reference, char* target, nk_u32 capacity);
int nk_remove(struct nk_volume* volume, nk_u64 directory, const char* name, nk_u32 length, nk_u64 child,
              int is_directory, int keep_record, nk_u32* links_left);
int nk_delete_unlinked(struct nk_volume* volume, nk_u64 reference);
int nk_rename(struct nk_volume* volume, nk_u64 old_directory, const char* old_name, nk_u32 old_length,
              nk_u64 new_directory, const char* new_name, nk_u32 new_length);
typedef int (*nk_emit)(void* context, const char* name, nk_u32 length, nk_u64 reference, int kind);
int nk_readdir(struct nk_volume* volume, nk_u64 directory, nk_u64 skip, nk_emit emit, void* context);
long nk_read(struct nk_volume* volume, nk_u64 reference, nk_u64 offset, void* buffer, nk_u32 length);
long nk_write(struct nk_volume* volume, nk_u64 reference, nk_u64 offset, const void* buffer, nk_u32 length,
              struct nk_attr* after);
int nk_resize(struct nk_volume* volume, nk_u64 reference, nk_u64 size, struct nk_attr* after);
/* Device bytes behind file bytes from offset on; run is 0 where nothing is allocated. */
int nk_map(struct nk_volume* volume, nk_u64 reference, nk_u64 offset, nk_u64* disk, nk_u64* run);
int nk_reserve(struct nk_volume* volume, nk_u64 reference, nk_u64 allocation, struct nk_attr* after);
int nk_commit(struct nk_volume* volume, nk_u64 reference, nk_u64 size, nk_u64 valid, int trim, nk_u64 accessed,
              nk_u64 written, nk_u64 changed, struct nk_attr* after);
/* Device byte where the record of a file starts; 0 when it cannot be told. */
nk_u64 nk_record_offset(struct nk_volume* volume, nk_u64 reference);
int nk_set_times(struct nk_volume* volume, nk_u64 reference, nk_u64 accessed, nk_u64 written, nk_u64 changed);

#ifdef __cplusplus
}
#endif

#endif
