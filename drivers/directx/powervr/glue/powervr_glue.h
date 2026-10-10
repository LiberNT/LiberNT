/*
 * PROJECT:     LiberNT PowerVR Rogue WDDM miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interface between the WDDM miniport and the imported PowerVR core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdint.h>

struct pvr_glue_platform
{
    uint64_t registers_physical;
    uint64_t registers_length;
    uint32_t interrupt_vector;
    uint8_t interrupt_irql;
    uint8_t interrupt_level_sensitive;
    uint64_t core_clock_hz;
    uint64_t dma_limit;
    const char *const *compatible;
    unsigned int compatible_count;
};

struct pvr_glue_info
{
    uint16_t branch;
    uint16_t version;
    uint16_t scalable_units;
    uint16_t config;
    uint32_t firmware_major;
    uint32_t firmware_minor;
    uint32_t firmware_booted;
};

int pvr_glue_driver_init(void);
void pvr_glue_driver_exit(void);
int pvr_glue_probe(const struct pvr_glue_platform *platform, void **instance);
void pvr_glue_remove(void *instance);
int pvr_glue_query(void *instance, struct pvr_glue_info *info);
int pvr_glue_isr(void *instance, int *queue_dpc);
void pvr_glue_dpc(void *instance);
int pvr_glue_open(void *instance, void **file);
void pvr_glue_close(void *file);
long pvr_glue_ioctl(void *file, uint32_t cmd, void *user_arg, uint32_t arg_size);
long pvr_glue_sync_merge(void *file, uint32_t destination, uint64_t destination_point, uint32_t count,
                         const uint32_t *handles, const uint64_t *points);
int pvr_glue_mmap(void *file, uint64_t offset, uint64_t size, uint64_t *user_address);
int pvr_glue_munmap(void *file, uint64_t user_address);
int pvr_glue_import(void *file, uint64_t physical, uint64_t size, void (*release)(void *context), void *context,
                    uint32_t *handle);
void *pvr_glue_import_acquire(void *file, uint32_t handle, void (*release)(void *context),
                              void (*acquire)(void *context));
void *pvr_glue_syncobj_fence(void *file, uint32_t syncobj);
void pvr_glue_fence_put(void *fence);
int pvr_glue_fence_notify(void *fence, void (*func)(void *context), void *context, void **wait);
int pvr_glue_fence_cancel(void *fence, void *wait);
void pvr_glue_fence_end(void *fence, void *wait);
