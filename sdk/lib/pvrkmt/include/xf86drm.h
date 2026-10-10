/*
 * PROJECT:     LiberNT PowerVR user-mode transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DRM device, ioctl and sync object interface over D3DKMT escapes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <drm-uapi/drm.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DRM_NODE_PRIMARY 0
#define DRM_NODE_CONTROL 1
#define DRM_NODE_RENDER 2
#define DRM_NODE_MAX 3

#define DRM_BUS_PCI 0
#define DRM_BUS_USB 1
#define DRM_BUS_PLATFORM 2
#define DRM_BUS_HOST1X 3

#ifndef DRM_RDWR
#define DRM_RDWR 0x0002
#endif
#ifndef DRM_CLOEXEC
#define DRM_CLOEXEC 0x80000
#endif

#define DRM_DEVICE_GET_PCI_REVISION (1 << 0)

typedef struct _drmVersion
{
    int version_major;
    int version_minor;
    int version_patchlevel;
    int name_len;
    char *name;
    int date_len;
    char *date;
    int desc_len;
    char *desc;
} drmVersion, *drmVersionPtr;

typedef struct _drmPlatformBusInfo
{
    char fullname[512];
} drmPlatformBusInfo, *drmPlatformBusInfoPtr;

typedef struct _drmPlatformDeviceInfo
{
    char **compatible;
} drmPlatformDeviceInfo, *drmPlatformDeviceInfoPtr;

typedef struct _drmDevice
{
    char **nodes;
    int available_nodes;
    int bustype;
    union
    {
        void *pci;
        void *usb;
        drmPlatformBusInfoPtr platform;
        void *host1x;
    } businfo;
    union
    {
        void *pci;
        void *usb;
        drmPlatformDeviceInfoPtr platform;
        void *host1x;
    } deviceinfo;
} drmDevice, *drmDevicePtr;

int drmIoctl(int fd, unsigned long request, void *arg);
drmVersionPtr drmGetVersion(int fd);
void drmFreeVersion(drmVersionPtr version);
int drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices);
void drmFreeDevices(drmDevicePtr devices[], int count);
int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device);
void drmFreeDevice(drmDevicePtr *device);
int drmGetCap(int fd, uint64_t capability, uint64_t *value);
int drmDropMaster(int fd);
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle);
int drmCloseBufferHandle(int fd, uint32_t handle);

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd);
int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle);
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd);
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd);
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles, int64_t timeout_nsec, unsigned flags,
                   uint32_t *first_signaled);
int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t handle_count);
int drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t handle_count);
int drmSyncobjTimelineSignal(int fd, const uint32_t *handles, uint64_t *points, uint32_t handle_count);
int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned num_handles, int64_t timeout_nsec,
                           unsigned flags, uint32_t *first_signaled);
int drmSyncobjQuery(int fd, uint32_t *handles, uint64_t *points, uint32_t handle_count);
int drmSyncobjQuery2(int fd, uint32_t *handles, uint64_t *points, uint32_t handle_count, uint32_t flags);
int drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point, uint32_t src_handle, uint64_t src_point,
                       uint32_t flags);
int drmSyncobjEventfd(int fd, uint32_t handle, uint64_t point, int ev_fd, uint32_t flags);

int pvrkmt_open(const char *path, int flags);
int pvrkmt_close(int fd);
int pvrkmt_is_fd(int fd);
int pvrkmt_scanout_query(int fd, uint32_t *width, uint32_t *height);
int pvrkmt_scanout_create(int fd, uint64_t size, uint32_t *handle, uint64_t *actual_size);
int pvrkmt_scanout_present(int fd, uint32_t handle, uint32_t syncobj, int32_t left, int32_t top, uint32_t width,
                           uint32_t height, uint32_t pitch, int wait, uint32_t busy[3]);
int pvrkmt_syncobj_merge(int fd, uint32_t destination, uint64_t destination_point, const uint32_t *handles,
                         const uint64_t *points, uint32_t count);

#ifdef __cplusplus
}
#endif
