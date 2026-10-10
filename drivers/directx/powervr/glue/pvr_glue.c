/*
 * PROJECT:     LiberNT PowerVR Rogue WDDM miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Platform device, interrupt and DRM file plumbing for the imported PowerVR core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <linux_compat.h>
#include <drm_compat.h>
#include <drm/drm_syncobj.h>

#include "pvr_device.h"
#include "pvr_gem.h"
#include "powervr_glue.h"

struct pvr_glue_instance
{
    struct platform_device pdev;
    struct resource resources[2];
    struct device_node of_node;
    struct platform_driver *driver;
    unsigned int irq;
    u64 core_clock_hz;
    bool probed;
};

static const u32 pvr_glue_power_domain[] = { 0 };
static const struct lc_of_property pvr_glue_properties[] = {
    { "power-domains", pvr_glue_power_domain, ARRAY_SIZE(pvr_glue_power_domain), 1 },
};

int lc_module_init_drm_sched_fence_slab_init(void);
void lc_module_exit_drm_sched_fence_slab_fini(void);
void *lc_module_param_exp_hw_support(void);

static int pvr_glue_clk_enable(void *context)
{
    (void)context;
    return 0;
}

static void pvr_glue_clk_disable(void *context)
{
    (void)context;
}

static unsigned long pvr_glue_clk_rate(void *context)
{
    struct pvr_glue_instance *glue = context;

    return (unsigned long)glue->core_clock_hz;
}

static const struct lc_clk_ops pvr_glue_core_clk_ops = {
    .enable = pvr_glue_clk_enable,
    .disable = pvr_glue_clk_disable,
    .get_rate = pvr_glue_clk_rate,
};

int pvr_glue_driver_init(void)
{
    int err;

    err = lc_compat_init();
    if (err)
        return err;
    err = lc_module_init_drm_sched_fence_slab_init();
    if (err)
        lc_compat_exit();
    return err;
}

void pvr_glue_driver_exit(void)
{
    lc_module_exit_drm_sched_fence_slab_fini();
    lc_compat_exit();
}

int pvr_glue_probe(const struct pvr_glue_platform *platform, void **instance)
{
    struct pvr_glue_instance *glue;
    int irq, err;

    *instance = NULL;
    glue = kzalloc(sizeof(*glue), GFP_KERNEL);
    if (!glue)
        return -ENOMEM;

    lc_set_page_allocation_limit(platform->dma_limit);
    *(bool *)lc_module_param_exp_hw_support() = true;

    irq = lc_irq_register_line(platform->interrupt_vector, platform->interrupt_irql, platform->interrupt_level_sensitive);
    if (irq < 0)
    {
        kfree(glue);
        return irq;
    }
    glue->irq = (unsigned int)irq;
    glue->core_clock_hz = platform->core_clock_hz;
    glue->driver = lc_module_platform_driver();

    glue->resources[0].start = platform->registers_physical;
    glue->resources[0].end = platform->registers_physical + platform->registers_length - 1;
    glue->resources[0].flags = IORESOURCE_MEM;
    glue->resources[0].name = "gpu";
    glue->resources[1].start = (resource_size_t)irq;
    glue->resources[1].end = (resource_size_t)irq;
    glue->resources[1].flags = IORESOURCE_IRQ;
    glue->resources[1].name = "gpu";

    glue->of_node.compatible = platform->compatible;
    glue->of_node.compatible_count = platform->compatible_count;
    glue->of_node.name = "gpu";
    glue->of_node.properties = pvr_glue_properties;
    glue->of_node.property_count = ARRAY_SIZE(pvr_glue_properties);

    glue->pdev.name = "powervr";
    glue->pdev.id = -1;
    glue->pdev.resource = glue->resources;
    glue->pdev.num_resources = ARRAY_SIZE(glue->resources);
    lc_device_initialize(&glue->pdev.dev, "powervr");
    glue->pdev.dev.of_node = &glue->of_node;
    glue->pdev.dev.driver = &glue->driver->driver;
    if (!lc_device_add_clk(&glue->pdev.dev, "core", &pvr_glue_core_clk_ops, glue))
    {
        kfree(glue);
        return -ENOMEM;
    }

    err = glue->driver->probe(&glue->pdev);
    if (err)
    {
        dev_err(&glue->pdev.dev, "probe failed (%d)\n", err);
        lc_device_release_resources(&glue->pdev.dev);
        kfree(glue);
        return err;
    }
    glue->probed = true;
    *instance = glue;
    return 0;
}

void pvr_glue_remove(void *instance)
{
    struct pvr_glue_instance *glue = instance;

    if (!glue)
        return;
    if (glue->probed && glue->driver->remove)
        glue->driver->remove(&glue->pdev);
    lc_device_release_resources(&glue->pdev.dev);
    kfree(glue);
}

int pvr_glue_query(void *instance, struct pvr_glue_info *info)
{
    struct pvr_glue_instance *glue = instance;
    struct drm_device *drm;
    struct pvr_device *pvr_dev;

    if (!glue || !glue->probed)
        return -ENODEV;
    drm = platform_get_drvdata(&glue->pdev);
    pvr_dev = to_pvr_device(drm);
    info->branch = pvr_dev->gpu_id.b;
    info->version = pvr_dev->gpu_id.v;
    info->scalable_units = pvr_dev->gpu_id.n;
    info->config = pvr_dev->gpu_id.c;
    info->firmware_major = pvr_dev->fw_version.major;
    info->firmware_minor = pvr_dev->fw_version.minor;
    info->firmware_booted = READ_ONCE(pvr_dev->fw_dev.initialised) ? 1 : 0;
    return 0;
}

int pvr_glue_isr(void *instance, int *queue_dpc)
{
    struct pvr_glue_instance *glue = instance;
    bool dpc = false;
    bool handled;

    *queue_dpc = 0;
    if (!glue)
        return 0;
    handled = lc_irq_service(glue->irq, &dpc);
    *queue_dpc = dpc ? 1 : 0;
    return handled ? 1 : 0;
}

void pvr_glue_dpc(void *instance)
{
    struct pvr_glue_instance *glue = instance;

    if (glue)
        lc_irq_dpc(glue->irq);
}

int pvr_glue_open(void *instance, void **file)
{
    struct pvr_glue_instance *glue = instance;
    struct drm_file *drm_file;

    *file = NULL;
    if (!glue || !glue->probed)
        return -ENODEV;
    drm_file = lc_drm_file_open(platform_get_drvdata(&glue->pdev));
    if (IS_ERR(drm_file))
        return (int)PTR_ERR(drm_file);
    *file = drm_file;
    return 0;
}

void pvr_glue_close(void *file)
{
    if (file)
        lc_drm_file_close(file);
}

long pvr_glue_ioctl(void *file, uint32_t cmd, void *user_arg, uint32_t arg_size)
{
    if (!file)
        return -EBADF;
    return lc_drm_ioctl(file, cmd, user_arg, arg_size);
}

long pvr_glue_sync_merge(void *file, uint32_t destination, uint64_t destination_point, uint32_t count,
                         const uint32_t *handles, const uint64_t *points)
{
    struct drm_syncobj_create create = { 0 };
    struct drm_syncobj_destroy destroy = { 0 };
    struct drm_syncobj_transfer transfer;
    uint32_t index;
    long ret;

    if (!file)
        return -EBADF;
    if (count == 1)
    {
        memset(&transfer, 0, sizeof(transfer));
        transfer.src_handle = handles[0];
        transfer.src_point = points[0];
        transfer.dst_handle = destination;
        transfer.dst_point = destination_point;
        return lc_drm_ioctl_kernel(file, DRM_IOCTL_SYNCOBJ_TRANSFER, &transfer);
    }

    create.flags = count == 0 ? DRM_SYNCOBJ_CREATE_SIGNALED : 0;
    ret = lc_drm_ioctl_kernel(file, DRM_IOCTL_SYNCOBJ_CREATE, &create);
    if (ret)
        return ret;
    for (index = 0; index < count; index++)
    {
        memset(&transfer, 0, sizeof(transfer));
        transfer.src_handle = handles[index];
        transfer.src_point = points[index];
        transfer.dst_handle = create.handle;
        transfer.dst_point = index + 1;
        ret = lc_drm_ioctl_kernel(file, DRM_IOCTL_SYNCOBJ_TRANSFER, &transfer);
        if (ret)
            goto out;
    }
    memset(&transfer, 0, sizeof(transfer));
    transfer.src_handle = create.handle;
    transfer.src_point = count;
    transfer.dst_handle = destination;
    transfer.dst_point = destination_point;
    ret = lc_drm_ioctl_kernel(file, DRM_IOCTL_SYNCOBJ_TRANSFER, &transfer);
out:
    destroy.handle = create.handle;
    lc_drm_ioctl_kernel(file, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy);
    return ret;
}

int pvr_glue_mmap(void *file, uint64_t offset, uint64_t size, uint64_t *user_address)
{
    if (!file)
        return -EBADF;
    return lc_drm_mmap(file, offset, size, user_address);
}

int pvr_glue_munmap(void *file, uint64_t user_address)
{
    if (!file)
        return -EBADF;
    return lc_drm_munmap(file, user_address);
}

struct pvr_glue_fence_wait
{
    struct dma_fence_cb cb;
    void (*func)(void *context);
    void *context;
};

int pvr_glue_import(void *file, uint64_t physical, uint64_t size, void (*release)(void *context), void *context,
                    uint32_t *handle)
{
    struct drm_file *drm_file = file;
    struct drm_gem_shmem_object *shmem;
    struct pvr_gem_object *pvr_obj;
    struct pvr_file *pvr_file;
    int err;

    *handle = 0;
    if (!file || !release || (physical & (PAGE_SIZE - 1)) || !size || (size & (PAGE_SIZE - 1)))
    {
        if (release)
            release(context);
        return -EINVAL;
    }
    pvr_file = to_pvr_file(drm_file);
    shmem = drm_gem_shmem_lc_import(from_pvr_device(pvr_file->pvr_dev), physical >> PAGE_SHIFT, (size_t)size, release,
                                    context);
    if (IS_ERR(shmem))
    {
        release(context);
        return (int)PTR_ERR(shmem);
    }
    pvr_obj = shmem_gem_to_pvr_gem(shmem);
    pvr_obj->flags = DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS;
    err = pvr_gem_object_into_handle(pvr_obj, pvr_file, handle);
    if (err)
    {
        pvr_gem_object_put(pvr_obj);
        *handle = 0;
    }
    return err;
}

void *pvr_glue_import_acquire(void *file, uint32_t handle, void (*release)(void *context),
                              void (*acquire)(void *context))
{
    struct drm_gem_shmem_object *shmem;
    struct drm_gem_object *obj;
    void *context = NULL;

    if (!file || !handle)
        return NULL;
    obj = drm_gem_object_lookup(file, handle);
    if (!obj)
        return NULL;
    shmem = to_drm_gem_shmem_obj(obj);
    if (shmem->lc_import_release == release)
    {
        context = shmem->lc_import_context;
        acquire(context);
    }
    drm_gem_object_put(obj);
    return context;
}

void *pvr_glue_syncobj_fence(void *file, uint32_t syncobj)
{
    struct dma_fence *fence = NULL;

    if (!file || !syncobj || drm_syncobj_find_fence(file, syncobj, 0, 0, &fence))
        return NULL;
    return fence;
}

void pvr_glue_fence_put(void *fence)
{
    dma_fence_put(fence);
}

static void pvr_glue_fence_wait_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
    struct pvr_glue_fence_wait *wait = container_of(cb, struct pvr_glue_fence_wait, cb);

    (void)fence;
    wait->func(wait->context);
}

int pvr_glue_fence_notify(void *fence, void (*func)(void *context), void *context, void **wait)
{
    struct pvr_glue_fence_wait *record;

    *wait = NULL;
    if (!fence)
        return 1;
    record = kzalloc(sizeof(*record), GFP_KERNEL);
    if (!record)
        return -ENOMEM;
    record->func = func;
    record->context = context;
    if (dma_fence_add_callback(fence, &record->cb, pvr_glue_fence_wait_cb))
    {
        kfree(record);
        return 1;
    }
    *wait = record;
    return 0;
}

int pvr_glue_fence_cancel(void *fence, void *wait)
{
    struct pvr_glue_fence_wait *record = wait;

    if (!fence || !record)
        return 0;
    return dma_fence_remove_callback(fence, &record->cb) ? 1 : 0;
}

void pvr_glue_fence_end(void *fence, void *wait)
{
    struct pvr_glue_fence_wait *record = wait;

    if (!record)
        return;
    if (fence)
        (void)dma_fence_remove_callback(fence, &record->cb);
    kfree(record);
}
