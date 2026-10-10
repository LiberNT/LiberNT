/*
 * PROJECT:     LiberNT PowerVR user-mode transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DRM render node emulation for Mesa's PowerVR driver over D3DKMT escapes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS

#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0xF003
#endif

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <d3dkmthk.h>
#include <errno.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <reactos/powervr_umd.h>

#include <drm-uapi/drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <sys/mman.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef _IOC_SIZE
#define _IOC_SIZE(nr) (((nr) >> _IOC_SIZESHIFT) & ((1U << _IOC_SIZEBITS) - 1))
#endif

#define PVRKMT_FD_BASE 0x4000
#define PVRKMT_MAX_FDS 64
#define PVRKMT_MAX_MAPPINGS 4096
#define PVRKMT_NODE_PREFIX "pvrkmt:"

typedef struct _PVRKMT_FILE
{
    BOOL InUse;
    LUID Luid;
    D3DKMT_HANDLE Adapter;
    D3DKMT_HANDLE Device;
    BOOL NoSyncMerge;
} PVRKMT_FILE;

typedef struct _PVRKMT_MAPPING
{
    void *Address;
    size_t Length;
    int Fd;
} PVRKMT_MAPPING;

static PVRKMT_FILE PvrKmtFiles[PVRKMT_MAX_FDS];
static PVRKMT_MAPPING PvrKmtMappings[PVRKMT_MAX_MAPPINGS];
static SRWLOCK PvrKmtLock = SRWLOCK_INIT;

static int
PvrKmtErrno(LONG LinuxError)
{
    switch (LinuxError)
    {
        case 1: return EPERM;
        case 2: return ENOENT;
        case 3: return ESRCH;
        case 4: return EINTR;
        case 5: return EIO;
        case 6: return ENXIO;
        case 7: return E2BIG;
        case 8: return ENOEXEC;
        case 9: return EBADF;
        case 11: return EAGAIN;
        case 12: return ENOMEM;
        case 13: return EACCES;
        case 14: return EFAULT;
        case 16: return EBUSY;
        case 17: return EEXIST;
        case 19: return ENODEV;
        case 22: return EINVAL;
        case 24: return EMFILE;
        case 25: return ENOTTY;
        case 28: return ENOSPC;
        case 32: return EPIPE;
        case 34: return ERANGE;
        case 35: return EDEADLK;
        case 36: return ENAMETOOLONG;
        case 38: return ENOSYS;
        case 39: return ENOTEMPTY;
        case 61: return ENODATA;
        case 62: return ETIME;
        case 75: return EOVERFLOW;
        case 95: return EOPNOTSUPP;
        case 110: return ETIMEDOUT;
        case 114: return EALREADY;
        case 125: return ECANCELED;
        case 512: return EINTR;
        case 524: return ENOTSUP;
        default: return EIO;
    }
}

static PVRKMT_FILE *
PvrKmtFile(int Fd)
{
    if (Fd < PVRKMT_FD_BASE || Fd >= PVRKMT_FD_BASE + PVRKMT_MAX_FDS)
        return NULL;
    if (!PvrKmtFiles[Fd - PVRKMT_FD_BASE].InUse)
        return NULL;
    return &PvrKmtFiles[Fd - PVRKMT_FD_BASE];
}

int
pvrkmt_is_fd(int fd)
{
    return PvrKmtFile(fd) != NULL;
}

static void
PvrKmtHeader(POWERVR_ESCAPE_HEADER *Header, ULONG Command, ULONG Size)
{
    memset(Header, 0, Size);
    Header->Magic = POWERVR_ESCAPE_MAGIC;
    Header->AbiVersion = POWERVR_ESCAPE_ABI_VERSION;
    Header->Command = Command;
    Header->Size = Size;
}

static NTSTATUS
PvrKmtEscape(D3DKMT_HANDLE Adapter, D3DKMT_HANDLE Device, void *Data, UINT Size)
{
    D3DKMT_ESCAPE Escape;

    memset(&Escape, 0, sizeof(Escape));
    Escape.hAdapter = Adapter;
    Escape.hDevice = Device;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.Flags.NoAdapterSynchronization = 1;
    Escape.pPrivateDriverData = Data;
    Escape.PrivateDriverDataSize = Size;
    return D3DKMTEscape(&Escape);
}

static BOOL
PvrKmtAdapterIsPowerVr(D3DKMT_HANDLE Adapter)
{
    POWERVR_ESCAPE_INFO Info;

    PvrKmtHeader(&Info.Header, POWERVR_ESCAPE_QUERY_INFO, sizeof(Info));
    if (!NT_SUCCESS(PvrKmtEscape(Adapter, 0, &Info, sizeof(Info))) || Info.Header.Status != 0)
        return FALSE;
    return Info.Branch != 0 && Info.FirmwareState != 0;
}

static int
PvrKmtSetError(LONG LinuxError)
{
    errno = PvrKmtErrno(LinuxError);
    return -1;
}

int
pvrkmt_open(const char *path, int flags)
{
    D3DKMT_OPENADAPTERFROMLUID Open;
    D3DKMT_CREATEDEVICE Create;
    D3DKMT_CLOSEADAPTER Close;
    unsigned long long Luid;
    char *End;
    int Index;

    (void)flags;
    if (!path || strncmp(path, PVRKMT_NODE_PREFIX, strlen(PVRKMT_NODE_PREFIX)))
    {
        errno = ENOENT;
        return -1;
    }
    Luid = strtoull(path + strlen(PVRKMT_NODE_PREFIX), &End, 16);
    if (*End)
    {
        errno = ENOENT;
        return -1;
    }

    memset(&Open, 0, sizeof(Open));
    Open.AdapterLuid.LowPart = (DWORD)Luid;
    Open.AdapterLuid.HighPart = (LONG)(Luid >> 32);
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromLuid(&Open)))
    {
        errno = ENODEV;
        return -1;
    }

    memset(&Create, 0, sizeof(Create));
    Create.hAdapter = Open.hAdapter;
    if (!NT_SUCCESS(D3DKMTCreateDevice(&Create)))
    {
        Close.hAdapter = Open.hAdapter;
        D3DKMTCloseAdapter(&Close);
        errno = ENODEV;
        return -1;
    }

    AcquireSRWLockExclusive(&PvrKmtLock);
    for (Index = 0; Index < PVRKMT_MAX_FDS; ++Index)
    {
        if (!PvrKmtFiles[Index].InUse)
        {
            PvrKmtFiles[Index].InUse = TRUE;
            PvrKmtFiles[Index].Luid = Open.AdapterLuid;
            PvrKmtFiles[Index].Adapter = Open.hAdapter;
            PvrKmtFiles[Index].Device = Create.hDevice;
            ReleaseSRWLockExclusive(&PvrKmtLock);
            return PVRKMT_FD_BASE + Index;
        }
    }
    ReleaseSRWLockExclusive(&PvrKmtLock);

    {
        D3DKMT_DESTROYDEVICE Destroy;

        Destroy.hDevice = Create.hDevice;
        D3DKMTDestroyDevice(&Destroy);
        Close.hAdapter = Open.hAdapter;
        D3DKMTCloseAdapter(&Close);
    }
    errno = EMFILE;
    return -1;
}

int
pvrkmt_close(int fd)
{
    D3DKMT_DESTROYDEVICE Destroy;
    D3DKMT_CLOSEADAPTER Close;
    PVRKMT_FILE File;
    PVRKMT_FILE *Entry;
    int Index;

    AcquireSRWLockExclusive(&PvrKmtLock);
    Entry = PvrKmtFile(fd);
    if (!Entry)
    {
        ReleaseSRWLockExclusive(&PvrKmtLock);
        return _close(fd);
    }
    File = *Entry;
    memset(Entry, 0, sizeof(*Entry));
    for (Index = 0; Index < PVRKMT_MAX_MAPPINGS; ++Index)
    {
        if (PvrKmtMappings[Index].Address && PvrKmtMappings[Index].Fd == fd)
            memset(&PvrKmtMappings[Index], 0, sizeof(PvrKmtMappings[Index]));
    }
    ReleaseSRWLockExclusive(&PvrKmtLock);

    Destroy.hDevice = File.Device;
    D3DKMTDestroyDevice(&Destroy);
    Close.hAdapter = File.Adapter;
    D3DKMTCloseAdapter(&Close);
    return 0;
}

int
pvrkmt_scanout_query(int fd, uint32_t *width, uint32_t *height)
{
    PVRKMT_FILE *File = PvrKmtFile(fd);
    POWERVR_ESCAPE_INFO Info;

    if (!File)
    {
        errno = EBADF;
        return -1;
    }
    PvrKmtHeader(&Info.Header, POWERVR_ESCAPE_QUERY_INFO, sizeof(Info));
    if (!NT_SUCCESS(PvrKmtEscape(File->Adapter, File->Device, &Info, sizeof(Info))) || Info.Header.Status != 0 ||
        !(Info.Caps & POWERVR_CAP_SCANOUT))
    {
        errno = ENOTSUP;
        return -1;
    }
    *width = Info.ScreenWidth;
    *height = Info.ScreenHeight;
    return 0;
}

int
pvrkmt_scanout_create(int fd, uint64_t size, uint32_t *handle, uint64_t *actual_size)
{
    PVRKMT_FILE *File = PvrKmtFile(fd);
    POWERVR_SCANOUT_CREATE_ESCAPE Create;

    if (!File)
    {
        errno = EBADF;
        return -1;
    }
    PvrKmtHeader(&Create.Header, POWERVR_ESCAPE_SCANOUT_CREATE, sizeof(Create));
    Create.Size = size;
    if (!NT_SUCCESS(PvrKmtEscape(File->Adapter, File->Device, &Create, sizeof(Create))) ||
        !NT_SUCCESS(Create.Header.Status) || !Create.Handle)
    {
        errno = ENOMEM;
        return -1;
    }
    *handle = Create.Handle;
    *actual_size = Create.Size;
    return 0;
}

int
pvrkmt_scanout_present(int fd, uint32_t handle, uint32_t syncobj, int32_t left, int32_t top, uint32_t width,
                       uint32_t height, uint32_t pitch, int wait, uint32_t busy[3])
{
    PVRKMT_FILE *File = PvrKmtFile(fd);
    POWERVR_SCANOUT_PRESENT_ESCAPE Present;
    int Index;

    if (!File)
    {
        errno = EBADF;
        return -1;
    }
    PvrKmtHeader(&Present.Header, POWERVR_ESCAPE_SCANOUT_PRESENT, sizeof(Present));
    Present.Handle = handle;
    Present.SyncObject = syncobj;
    Present.Left = left;
    Present.Top = top;
    Present.Width = width;
    Present.Height = height;
    Present.Pitch = pitch;
    Present.Flags = wait ? POWERVR_SCANOUT_PRESENT_WAIT : 0;
    if (!NT_SUCCESS(PvrKmtEscape(File->Adapter, File->Device, &Present, sizeof(Present))) ||
        !NT_SUCCESS(Present.Header.Status))
    {
        errno = EINVAL;
        return -1;
    }
    for (Index = 0; Index < POWERVR_SCANOUT_BUSY_COUNT; ++Index)
        busy[Index] = Present.Busy[Index];
    return 0;
}

int
pvrkmt_syncobj_merge(int fd, uint32_t destination, uint64_t destination_point, const uint32_t *handles,
                     const uint64_t *points, uint32_t count)
{
    PVRKMT_FILE *File = PvrKmtFile(fd);
    POWERVR_SYNC_MERGE_ESCAPE Merge;
    uint32_t Index;

    if (!File)
    {
        errno = EBADF;
        return -1;
    }
    if (count > POWERVR_SYNC_MERGE_COUNT || File->NoSyncMerge)
    {
        errno = ENOSYS;
        return -1;
    }
    PvrKmtHeader(&Merge.Header, POWERVR_ESCAPE_SYNC_MERGE, sizeof(Merge));
    Merge.Destination = destination;
    Merge.DestinationPoint = destination_point;
    Merge.Count = count;
    for (Index = 0; Index < count; Index++)
    {
        Merge.Handles[Index] = handles[Index];
        Merge.Points[Index] = points[Index];
    }
    if (!NT_SUCCESS(PvrKmtEscape(File->Adapter, File->Device, &Merge, sizeof(Merge))) ||
        Merge.Header.Status == STATUS_NOT_SUPPORTED)
    {
        File->NoSyncMerge = TRUE;
        errno = ENOSYS;
        return -1;
    }
    if (Merge.Header.Status < 0)
        return PvrKmtSetError(-Merge.Header.Status);
    return 0;
}

static LONG
PvrKmtIoctl(PVRKMT_FILE *File, unsigned long Request, void *Arg)
{
    POWERVR_DRM_IOCTL_ESCAPE Escape;

    PvrKmtHeader(&Escape.Header, POWERVR_ESCAPE_DRM_IOCTL, sizeof(Escape));
    Escape.Command = (ULONG)Request;
    Escape.ArgumentSize = _IOC_SIZE(Request);
    Escape.Argument = (ULONG64)(ULONG_PTR)Arg;
    if (!NT_SUCCESS(PvrKmtEscape(File->Adapter, File->Device, &Escape, sizeof(Escape))))
        return -19;
    return Escape.Header.Status;
}

int
drmIoctl(int fd, unsigned long request, void *arg)
{
    PVRKMT_FILE *File = PvrKmtFile(fd);
    LONG Result;

    if (!File)
    {
        errno = EBADF;
        return -1;
    }
    do
    {
        Result = PvrKmtIoctl(File, request, arg);
    } while (Result == -4 || Result == -11 || Result == -512);
    if (Result < 0)
        return PvrKmtSetError(-Result);
    return Result;
}

static char *
PvrKmtStrdup(const char *Text, size_t Length)
{
    char *Copy = malloc(Length + 1);

    if (!Copy)
        return NULL;
    memcpy(Copy, Text, Length);
    Copy[Length] = 0;
    return Copy;
}

drmVersionPtr
drmGetVersion(int fd)
{
    struct drm_version Query;
    char Name[64], Date[64], Desc[128];
    drmVersionPtr Version;

    memset(&Query, 0, sizeof(Query));
    Query.name = Name;
    Query.name_len = sizeof(Name) - 1;
    Query.date = Date;
    Query.date_len = sizeof(Date) - 1;
    Query.desc = Desc;
    Query.desc_len = sizeof(Desc) - 1;
    if (drmIoctl(fd, DRM_IOCTL_VERSION, &Query))
        return NULL;

    Version = calloc(1, sizeof(*Version));
    if (!Version)
        return NULL;
    Version->version_major = Query.version_major;
    Version->version_minor = Query.version_minor;
    Version->version_patchlevel = Query.version_patchlevel;
    Version->name_len = (int)min(Query.name_len, sizeof(Name) - 1);
    Version->date_len = (int)min(Query.date_len, sizeof(Date) - 1);
    Version->desc_len = (int)min(Query.desc_len, sizeof(Desc) - 1);
    Version->name = PvrKmtStrdup(Name, Version->name_len);
    Version->date = PvrKmtStrdup(Date, Version->date_len);
    Version->desc = PvrKmtStrdup(Desc, Version->desc_len);
    if (!Version->name || !Version->date || !Version->desc)
    {
        drmFreeVersion(Version);
        return NULL;
    }
    return Version;
}

void
drmFreeVersion(drmVersionPtr version)
{
    if (!version)
        return;
    free(version->name);
    free(version->date);
    free(version->desc);
    free(version);
}

static drmDevicePtr
PvrKmtNewDevice(LUID Luid)
{
    struct
    {
        drmDevice Device;
        char *Nodes[DRM_NODE_MAX];
        drmPlatformBusInfo Bus;
        drmPlatformDeviceInfo Info;
        char *Compatible[2];
        char Node[64];
    } *Block = calloc(1, sizeof(*Block));

    if (!Block)
        return NULL;
    snprintf(Block->Node, sizeof(Block->Node), PVRKMT_NODE_PREFIX "%08lx%08lx", (unsigned long)Luid.HighPart,
             (unsigned long)Luid.LowPart);
    Block->Nodes[DRM_NODE_RENDER] = Block->Node;
    Block->Device.nodes = Block->Nodes;
    Block->Device.available_nodes = 1 << DRM_NODE_RENDER;
    Block->Device.bustype = DRM_BUS_PLATFORM;
    strcpy(Block->Bus.fullname, "/platform/powervr");
    Block->Compatible[0] = "img,img-rogue";
    Block->Info.compatible = Block->Compatible;
    Block->Device.businfo.platform = &Block->Bus;
    Block->Device.deviceinfo.platform = &Block->Info;
    return &Block->Device;
}

int
drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices)
{
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_ADAPTERINFO *Adapters;
    D3DKMT_CLOSEADAPTER Close;
    int Found = 0;
    ULONG Index;

    (void)flags;
    memset(&Enum, 0, sizeof(Enum));
    if (!NT_SUCCESS(D3DKMTEnumAdapters2(&Enum)) || !Enum.NumAdapters)
        return 0;
    Adapters = calloc(Enum.NumAdapters, sizeof(*Adapters));
    if (!Adapters)
        return -ENOMEM;
    Enum.pAdapters = Adapters;
    if (!NT_SUCCESS(D3DKMTEnumAdapters2(&Enum)))
    {
        free(Adapters);
        return 0;
    }

    for (Index = 0; Index < Enum.NumAdapters; ++Index)
    {
        if (PvrKmtAdapterIsPowerVr(Adapters[Index].hAdapter))
        {
            if (devices && Found < max_devices)
            {
                devices[Found] = PvrKmtNewDevice(Adapters[Index].AdapterLuid);
                if (!devices[Found])
                    break;
            }
            Found++;
        }
        Close.hAdapter = Adapters[Index].hAdapter;
        D3DKMTCloseAdapter(&Close);
    }
    for (++Index; Index < Enum.NumAdapters; ++Index)
    {
        Close.hAdapter = Adapters[Index].hAdapter;
        D3DKMTCloseAdapter(&Close);
    }
    free(Adapters);
    if (devices && Found > max_devices)
        Found = max_devices;
    return Found;
}

void
drmFreeDevices(drmDevicePtr devices[], int count)
{
    int Index;

    if (!devices)
        return;
    for (Index = 0; Index < count; ++Index)
        drmFreeDevice(&devices[Index]);
}

int
drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device)
{
    PVRKMT_FILE *File = PvrKmtFile(fd);

    (void)flags;
    if (!File)
        return -EBADF;
    *device = PvrKmtNewDevice(File->Luid);
    return *device ? 0 : -ENOMEM;
}

void
drmFreeDevice(drmDevicePtr *device)
{
    if (device && *device)
    {
        free(*device);
        *device = NULL;
    }
}

int
drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
    struct drm_get_cap Cap;
    int Result;

    memset(&Cap, 0, sizeof(Cap));
    Cap.capability = capability;
    Result = drmIoctl(fd, DRM_IOCTL_GET_CAP, &Cap);
    if (Result)
        return Result;
    *value = Cap.value;
    return 0;
}

int
drmDropMaster(int fd)
{
    (void)fd;
    return 0;
}

int
drmIsKMS(int fd)
{
    (void)fd;
    return 0;
}

int
drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
    (void)fd;
    (void)handle;
    (void)flags;
    (void)prime_fd;
    errno = EOPNOTSUPP;
    return -EOPNOTSUPP;
}

int
drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
    (void)fd;
    (void)prime_fd;
    (void)handle;
    errno = EOPNOTSUPP;
    return -EOPNOTSUPP;
}

int
drmCloseBufferHandle(int fd, uint32_t handle)
{
    struct drm_gem_close Close;

    memset(&Close, 0, sizeof(Close));
    Close.handle = handle;
    return drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &Close);
}

int
drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
    struct drm_syncobj_create Args;
    int Result;

    memset(&Args, 0, sizeof(Args));
    Args.flags = flags;
    Result = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &Args);
    if (Result)
        return Result;
    *handle = Args.handle;
    return 0;
}

int
drmSyncobjDestroy(int fd, uint32_t handle)
{
    struct drm_syncobj_destroy Args;

    memset(&Args, 0, sizeof(Args));
    Args.handle = handle;
    return drmIoctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &Args);
}

int
drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd)
{
    (void)fd;
    (void)handle;
    (void)obj_fd;
    errno = EOPNOTSUPP;
    return -EOPNOTSUPP;
}

int
drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle)
{
    (void)fd;
    (void)obj_fd;
    (void)handle;
    errno = EOPNOTSUPP;
    return -EOPNOTSUPP;
}

int
drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd)
{
    (void)fd;
    (void)handle;
    (void)sync_file_fd;
    errno = EOPNOTSUPP;
    return -EOPNOTSUPP;
}

int
drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd)
{
    (void)fd;
    (void)handle;
    (void)sync_file_fd;
    errno = EOPNOTSUPP;
    return -EOPNOTSUPP;
}

int
drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles, int64_t timeout_nsec, unsigned flags,
               uint32_t *first_signaled)
{
    struct drm_syncobj_wait Args;
    int Result;

    memset(&Args, 0, sizeof(Args));
    Args.handles = (uint64_t)(uintptr_t)handles;
    Args.timeout_nsec = timeout_nsec;
    Args.count_handles = num_handles;
    Args.flags = flags;
    Result = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &Args);
    if (Result < 0)
        return -errno;
    if (first_signaled)
        *first_signaled = Args.first_signaled;
    return Result;
}

int
drmSyncobjReset(int fd, const uint32_t *handles, uint32_t handle_count)
{
    struct drm_syncobj_array Args;

    memset(&Args, 0, sizeof(Args));
    Args.handles = (uint64_t)(uintptr_t)handles;
    Args.count_handles = handle_count;
    return drmIoctl(fd, DRM_IOCTL_SYNCOBJ_RESET, &Args);
}

int
drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t handle_count)
{
    struct drm_syncobj_array Args;

    memset(&Args, 0, sizeof(Args));
    Args.handles = (uint64_t)(uintptr_t)handles;
    Args.count_handles = handle_count;
    return drmIoctl(fd, DRM_IOCTL_SYNCOBJ_SIGNAL, &Args);
}

int
drmSyncobjTimelineSignal(int fd, const uint32_t *handles, uint64_t *points, uint32_t handle_count)
{
    struct drm_syncobj_timeline_array Args;

    memset(&Args, 0, sizeof(Args));
    Args.handles = (uint64_t)(uintptr_t)handles;
    Args.points = (uint64_t)(uintptr_t)points;
    Args.count_handles = handle_count;
    return drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, &Args);
}

int
drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned num_handles, int64_t timeout_nsec,
                       unsigned flags, uint32_t *first_signaled)
{
    struct drm_syncobj_timeline_wait Args;
    int Result;

    memset(&Args, 0, sizeof(Args));
    Args.handles = (uint64_t)(uintptr_t)handles;
    Args.points = (uint64_t)(uintptr_t)points;
    Args.timeout_nsec = timeout_nsec;
    Args.count_handles = num_handles;
    Args.flags = flags;
    Result = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT, &Args);
    if (Result < 0)
        return -errno;
    if (first_signaled)
        *first_signaled = Args.first_signaled;
    return Result;
}

int
drmSyncobjQuery2(int fd, uint32_t *handles, uint64_t *points, uint32_t handle_count, uint32_t flags)
{
    struct drm_syncobj_timeline_array Args;

    memset(&Args, 0, sizeof(Args));
    Args.handles = (uint64_t)(uintptr_t)handles;
    Args.points = (uint64_t)(uintptr_t)points;
    Args.count_handles = handle_count;
    Args.flags = flags;
    return drmIoctl(fd, DRM_IOCTL_SYNCOBJ_QUERY, &Args);
}

int
drmSyncobjQuery(int fd, uint32_t *handles, uint64_t *points, uint32_t handle_count)
{
    return drmSyncobjQuery2(fd, handles, points, handle_count, 0);
}

int
drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point, uint32_t src_handle, uint64_t src_point,
                   uint32_t flags)
{
    struct drm_syncobj_transfer Args;

    memset(&Args, 0, sizeof(Args));
    Args.src_handle = src_handle;
    Args.dst_handle = dst_handle;
    Args.src_point = src_point;
    Args.dst_point = dst_point;
    Args.flags = flags;
    return drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TRANSFER, &Args);
}

int
drmSyncobjEventfd(int fd, uint32_t handle, uint64_t point, int ev_fd, uint32_t flags)
{
    (void)fd;
    (void)handle;
    (void)point;
    (void)ev_fd;
    (void)flags;
    errno = EOPNOTSUPP;
    return -EOPNOTSUPP;
}

static BOOL
PvrKmtRecordMapping(void *Address, size_t Length, int Fd)
{
    int Index;
    BOOL Recorded = FALSE;

    AcquireSRWLockExclusive(&PvrKmtLock);
    for (Index = 0; Index < PVRKMT_MAX_MAPPINGS; ++Index)
    {
        if (!PvrKmtMappings[Index].Address)
        {
            PvrKmtMappings[Index].Address = Address;
            PvrKmtMappings[Index].Length = Length;
            PvrKmtMappings[Index].Fd = Fd;
            Recorded = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive(&PvrKmtLock);
    return Recorded;
}

static LONG
PvrKmtMapEscape(PVRKMT_FILE *File, ULONG Command, uint64_t Offset, uint64_t Length, uint64_t *Address)
{
    POWERVR_DRM_MAP_ESCAPE Escape;

    PvrKmtHeader(&Escape.Header, Command, sizeof(Escape));
    Escape.Offset = Offset;
    Escape.Length = Length;
    Escape.Address = *Address;
    if (!NT_SUCCESS(PvrKmtEscape(File->Adapter, File->Device, &Escape, sizeof(Escape))))
        return -19;
    *Address = Escape.Address;
    return Escape.Header.Status;
}

void *
mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset)
{
    PVRKMT_FILE *File;
    uint64_t Address = 0;
    LONG Result;

    if (flags & MAP_ANONYMOUS)
    {
        void *Reserved = VirtualAlloc(addr, length, MEM_RESERVE | (prot != PROT_NONE ? MEM_COMMIT : 0),
                                      prot == PROT_NONE ? PAGE_NOACCESS : PAGE_READWRITE);

        if (!Reserved || ((flags & MAP_FIXED) && Reserved != addr))
        {
            if (Reserved)
                VirtualFree(Reserved, 0, MEM_RELEASE);
            errno = ENOMEM;
            return MAP_FAILED;
        }
        return Reserved;
    }

    File = PvrKmtFile(fd);
    if (!File)
    {
        errno = EBADF;
        return MAP_FAILED;
    }
    if (addr && (flags & MAP_FIXED))
    {
        errno = ENOTSUP;
        return MAP_FAILED;
    }

    Result = PvrKmtMapEscape(File, POWERVR_ESCAPE_DRM_MMAP, offset, length, &Address);
    if (Result < 0 || !Address)
    {
        errno = PvrKmtErrno(Result < 0 ? -Result : 12);
        return MAP_FAILED;
    }
    if (!PvrKmtRecordMapping((void *)(uintptr_t)Address, length, fd))
    {
        PvrKmtMapEscape(File, POWERVR_ESCAPE_DRM_MUNMAP, 0, 0, &Address);
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return (void *)(uintptr_t)Address;
}

int
munmap(void *addr, size_t length)
{
    PVRKMT_FILE *File = NULL;
    uint64_t Address = (uint64_t)(uintptr_t)addr;
    LONG Result;
    int Index, Fd = -1;

    (void)length;
    AcquireSRWLockExclusive(&PvrKmtLock);
    for (Index = 0; Index < PVRKMT_MAX_MAPPINGS; ++Index)
    {
        if (PvrKmtMappings[Index].Address == addr)
        {
            Fd = PvrKmtMappings[Index].Fd;
            memset(&PvrKmtMappings[Index], 0, sizeof(PvrKmtMappings[Index]));
            break;
        }
    }
    if (Fd >= 0)
        File = PvrKmtFile(Fd);
    ReleaseSRWLockExclusive(&PvrKmtLock);

    if (Fd < 0)
        return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : PvrKmtSetError(22);
    if (!File)
        return PvrKmtSetError(9);
    Result = PvrKmtMapEscape(File, POWERVR_ESCAPE_DRM_MUNMAP, 0, 0, &Address);
    return Result < 0 ? PvrKmtSetError(-Result) : 0;
}
