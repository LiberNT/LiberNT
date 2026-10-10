/*
 * PROJECT:     LiberNT DRM core library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DRM device lifetime, files, ioctl dispatch, managed resources and printing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <drm_compat.h>

struct drmm_resource
{
    struct list_head node;
    void (*action)(struct drm_device *dev, void *data);
    void *data;
};

struct lc_drm_mapping
{
    struct list_head node;
    struct drm_gem_object *obj;
    struct vm_area_struct vma;
};

static atomic_t lc_drm_inflight = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(lc_drm_inflight_wq);
static atomic64_t lc_drm_client_id = ATOMIC64_INIT(0);

int drm_gem_close_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
void drm_gem_release(struct drm_device *dev, struct drm_file *file);

static void drm_dev_release(struct kref *kref)
{
    struct drm_device *dev = container_of(kref, struct drm_device, ref);

    for (;;)
    {
        struct drmm_resource *res = NULL;

        spin_lock(&dev->lc_managed_lock);
        if (!list_empty(&dev->lc_managed))
        {
            res = list_first_entry(&dev->lc_managed, struct drmm_resource, node);
            list_del(&res->node);
        }
        spin_unlock(&dev->lc_managed_lock);
        if (!res)
            break;
        if (res->action)
            res->action(dev, res->data);
        kfree(res);
    }

    if (dev->driver->release)
        dev->driver->release(dev);
    xa_destroy(&dev->lc_mmap_offsets);
    kfree(dev->lc_allocation);
}

void drm_dev_get(struct drm_device *dev)
{
    if (dev)
        kref_get(&dev->ref);
}

void drm_dev_put(struct drm_device *dev)
{
    if (dev)
        kref_put(&dev->ref, drm_dev_release);
}

static void drm_dev_put_action(void *data)
{
    drm_dev_put(data);
}

struct drm_device *__devm_drm_dev_alloc(struct device *parent, const struct drm_driver *driver, size_t size, size_t offset)
{
    struct drm_device *dev;
    void *container;
    int err;

    container = kzalloc(size, GFP_KERNEL);
    if (!container)
        return ERR_PTR(-ENOMEM);
    dev = (struct drm_device *)((u8 *)container + offset);
    kref_init(&dev->ref);
    dev->dev = parent;
    dev->driver = driver;
    dev->lc_allocation = container;
    INIT_LIST_HEAD(&dev->lc_managed);
    spin_lock_init(&dev->lc_managed_lock);
    mutex_init(&dev->filelist_mutex);
    INIT_LIST_HEAD(&dev->filelist);
    xa_init_flags(&dev->lc_mmap_offsets, 0);
    dev->lc_next_mmap_page = 0x100000;

    err = devm_add_action_or_reset(parent, drm_dev_put_action, dev);
    if (err)
        return ERR_PTR(err);
    return (struct drm_device *)container;
}

int drm_dev_register(struct drm_device *dev, unsigned long flags)
{
    (void)flags;
    WRITE_ONCE(dev->registered, true);
    return 0;
}

void drm_dev_unregister(struct drm_device *dev)
{
    WRITE_ONCE(dev->registered, false);
}

bool drm_dev_enter(struct drm_device *dev, int *idx)
{
    *idx = 0;
    if (READ_ONCE(dev->unplugged))
        return false;
    atomic_inc(&lc_drm_inflight);
    smp_mb__after_atomic();
    if (READ_ONCE(dev->unplugged))
    {
        drm_dev_exit(0);
        return false;
    }
    return true;
}

void drm_dev_exit(int idx)
{
    (void)idx;
    if (atomic_dec_and_test(&lc_drm_inflight))
        wake_up_all(&lc_drm_inflight_wq);
}

void drm_dev_unplug(struct drm_device *dev)
{
    WRITE_ONCE(dev->unplugged, true);
    smp_mb();
    wait_event(lc_drm_inflight_wq, atomic_read(&lc_drm_inflight) == 0);
    drm_dev_unregister(dev);
}

void *drmm_kmalloc(struct drm_device *dev, size_t size, gfp_t gfp)
{
    struct drmm_resource *res = kmalloc(sizeof(*res) + size, gfp);

    if (!res)
        return NULL;
    res->action = NULL;
    res->data = res + 1;
    spin_lock(&dev->lc_managed_lock);
    list_add(&res->node, &dev->lc_managed);
    spin_unlock(&dev->lc_managed_lock);
    return res->data;
}

void drmm_kfree(struct drm_device *dev, void *data)
{
    struct drmm_resource *res, *found = NULL;

    if (!data)
        return;
    spin_lock(&dev->lc_managed_lock);
    list_for_each_entry(res, &dev->lc_managed, node)
    {
        if (!res->action && res->data == data)
        {
            list_del(&res->node);
            found = res;
            break;
        }
    }
    spin_unlock(&dev->lc_managed_lock);
    kfree(found);
}

int drmm_add_action(struct drm_device *dev, void (*action)(struct drm_device *, void *), void *data)
{
    struct drmm_resource *res = kzalloc(sizeof(*res), GFP_KERNEL);

    if (!res)
        return -ENOMEM;
    res->action = action;
    res->data = data;
    spin_lock(&dev->lc_managed_lock);
    list_add(&res->node, &dev->lc_managed);
    spin_unlock(&dev->lc_managed_lock);
    return 0;
}

int drmm_add_action_or_reset(struct drm_device *dev, void (*action)(struct drm_device *, void *), void *data)
{
    int err = drmm_add_action(dev, action, data);

    if (err)
        action(dev, data);
    return err;
}

static void drmm_mutex_release(struct drm_device *dev, void *data)
{
    (void)dev;
    mutex_destroy((struct mutex *)data);
}

int drmm_mutex_init(struct drm_device *dev, struct mutex *lock)
{
    mutex_init(lock);
    return drmm_add_action_or_reset(dev, drmm_mutex_release, lock);
}

struct drm_file *lc_drm_file_open(struct drm_device *dev)
{
    struct drm_file *file;
    int err;

    if (!drm_dev_enter(dev, &err))
        return ERR_PTR(-ENODEV);
    file = kzalloc(sizeof(*file), GFP_KERNEL);
    if (!file)
    {
        drm_dev_exit(0);
        return ERR_PTR(-ENOMEM);
    }
    file->dev = dev;
    file->client_id = (u64)atomic64_inc_return(&lc_drm_client_id);
    file->authenticated = true;
    file->lc_pid = (pid_t)lc_nt_current_pid();
    xa_init_flags(&file->object_idr, XA_FLAGS_ALLOC1);
    spin_lock_init(&file->table_lock);
    INIT_LIST_HEAD(&file->lhead);
    INIT_LIST_HEAD(&file->lc_mappings);
    mutex_init(&file->lc_mappings_lock);
    if (drm_core_check_feature(dev, DRIVER_SYNCOBJ))
        drm_syncobj_open(file);

    if (dev->driver->open)
    {
        err = dev->driver->open(dev, file);
        if (err)
        {
            if (drm_core_check_feature(dev, DRIVER_SYNCOBJ))
                drm_syncobj_release(file);
            xa_destroy(&file->object_idr);
            kfree(file);
            drm_dev_exit(0);
            return ERR_PTR(err);
        }
    }

    drm_dev_get(dev);
    mutex_lock(&dev->filelist_mutex);
    list_add(&file->lhead, &dev->filelist);
    mutex_unlock(&dev->filelist_mutex);
    drm_dev_exit(0);
    return file;
}

static void lc_drm_unmap(struct drm_file *file, struct lc_drm_mapping *mapping)
{
    (void)file;
    lc_nt_unmap_pfns((void *)mapping->vma.vm_start, mapping->vma.lc_cookie);
    drm_gem_object_put(mapping->obj);
    kfree(mapping);
}

void lc_drm_file_close(struct drm_file *file)
{
    struct drm_device *dev = file->dev;

    mutex_lock(&dev->filelist_mutex);
    list_del(&file->lhead);
    mutex_unlock(&dev->filelist_mutex);

    mutex_lock(&file->lc_mappings_lock);
    while (!list_empty(&file->lc_mappings))
    {
        struct lc_drm_mapping *mapping = list_first_entry(&file->lc_mappings, struct lc_drm_mapping, node);

        list_del(&mapping->node);
        if (file->lc_pid == (pid_t)lc_nt_current_pid())
        {
            lc_drm_unmap(file, mapping);
        }
        else
        {
            drm_gem_object_put(mapping->obj);
            kfree(mapping);
        }
    }
    mutex_unlock(&file->lc_mappings_lock);

    if (drm_core_check_feature(dev, DRIVER_SYNCOBJ))
        drm_syncobj_release(file);
    if (drm_core_check_feature(dev, DRIVER_GEM))
        drm_gem_release(dev, file);
    if (dev->driver->postclose)
        dev->driver->postclose(dev, file);
    xa_destroy(&file->object_idr);
    kfree(file);
    drm_dev_put(dev);
}

static int lc_copy_string(char __user *dst, __kernel_size_t *len, const char *src)
{
    size_t full = src ? strlen(src) : 0;
    size_t copy = min((size_t)*len, full);

    *len = full;
    if (copy && copy_to_user(dst, src, copy))
        return -EFAULT;
    return 0;
}

static int drm_version_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
    struct drm_version *version = data;
    int err;

    (void)file;
    version->version_major = dev->driver->major;
    version->version_minor = dev->driver->minor;
    version->version_patchlevel = dev->driver->patchlevel;
    err = lc_copy_string(version->name, &version->name_len, dev->driver->name);
    if (!err)
        err = lc_copy_string(version->date, &version->date_len, dev->driver->date ? dev->driver->date : "0");
    if (!err)
        err = lc_copy_string(version->desc, &version->desc_len, dev->driver->desc);
    return err;
}

static int drm_getcap_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
    struct drm_get_cap *req = data;

    (void)file;
    req->value = 0;
    switch (req->capability)
    {
        case DRM_CAP_TIMESTAMP_MONOTONIC:
            req->value = 1;
            return 0;
        case DRM_CAP_PRIME:
            return 0;
        case DRM_CAP_SYNCOBJ:
            req->value = drm_core_check_feature(dev, DRIVER_SYNCOBJ);
            return 0;
        case DRM_CAP_SYNCOBJ_TIMELINE:
            req->value = drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE);
            return 0;
        default:
            return -EINVAL;
    }
}

static int drm_unsupported_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
    (void)dev;
    (void)data;
    (void)file;
    return -EOPNOTSUPP;
}

#define LC_DRM_CORE_IOCTL(ioctl, fn) [DRM_IOCTL_NR(DRM_IOCTL_##ioctl)] = { .cmd = DRM_IOCTL_##ioctl, .func = fn, .name = #ioctl }

static const struct drm_ioctl_desc lc_drm_core_ioctls[DRM_COMMAND_BASE + 0x100] = {
    LC_DRM_CORE_IOCTL(VERSION, drm_version_ioctl),
    LC_DRM_CORE_IOCTL(GEM_CLOSE, drm_gem_close_ioctl),
    LC_DRM_CORE_IOCTL(GET_CAP, drm_getcap_ioctl),
    LC_DRM_CORE_IOCTL(PRIME_HANDLE_TO_FD, drm_unsupported_ioctl),
    LC_DRM_CORE_IOCTL(PRIME_FD_TO_HANDLE, drm_unsupported_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_CREATE, drm_syncobj_create_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_DESTROY, drm_syncobj_destroy_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_HANDLE_TO_FD, drm_syncobj_handle_to_fd_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_FD_TO_HANDLE, drm_syncobj_fd_to_handle_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_WAIT, drm_syncobj_wait_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_RESET, drm_syncobj_reset_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_SIGNAL, drm_syncobj_signal_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_TIMELINE_WAIT, drm_syncobj_timeline_wait_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_QUERY, drm_syncobj_query_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_TRANSFER, drm_syncobj_transfer_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_TIMELINE_SIGNAL, drm_syncobj_timeline_signal_ioctl),
    LC_DRM_CORE_IOCTL(SYNCOBJ_EVENTFD, drm_syncobj_eventfd_ioctl),
};

long lc_drm_ioctl(struct drm_file *file, unsigned int cmd, void __user *arg, unsigned int arg_size)
{
    struct drm_device *dev = file->dev;
    const struct drm_ioctl_desc *desc = NULL;
    unsigned int nr = DRM_IOCTL_NR(cmd);
    unsigned int in_size, out_size, drv_size, ksize;
    u8 stack_data[128];
    u8 *kdata;
    long ret;
    int idx;

    if (DRM_IOCTL_TYPE(cmd) != DRM_IOCTL_BASE)
        return -ENOTTY;
    if (nr >= DRM_COMMAND_BASE && nr < DRM_COMMAND_END)
    {
        unsigned int index = nr - DRM_COMMAND_BASE;

        if (index < (unsigned int)dev->driver->num_ioctls)
            desc = &dev->driver->ioctls[index];
    }
    else if (nr < ARRAY_SIZE(lc_drm_core_ioctls))
    {
        desc = &lc_drm_core_ioctls[nr];
    }
    if (!desc || !desc->func)
        return -EINVAL;

    drv_size = _IOC_SIZE(desc->cmd);
    out_size = in_size = _IOC_SIZE(cmd);
    if ((cmd & desc->cmd & IOC_IN) == 0)
        in_size = 0;
    if ((cmd & desc->cmd & IOC_OUT) == 0)
        out_size = 0;
    if (in_size > arg_size || out_size > arg_size)
        return -EINVAL;
    ksize = max(max(in_size, out_size), drv_size);

    kdata = ksize <= sizeof(stack_data) ? stack_data : kmalloc(ksize, GFP_KERNEL);
    if (!kdata)
        return -ENOMEM;
    if (in_size && copy_from_user(kdata, arg, in_size))
    {
        ret = -EFAULT;
        goto out;
    }
    if (ksize > in_size)
        memset(kdata + in_size, 0, ksize - in_size);

    if (!drm_dev_enter(dev, &idx))
    {
        ret = -ENODEV;
        goto out;
    }
    ret = desc->func(dev, kdata, file);
    drm_dev_exit(idx);

    if (out_size && copy_to_user(arg, kdata, out_size))
        ret = -EFAULT;
out:
    if (kdata != stack_data)
        kfree(kdata);
    return ret;
}

long lc_drm_ioctl_kernel(struct drm_file *file, unsigned int cmd, void *data)
{
    struct drm_device *dev = file->dev;
    unsigned int nr = DRM_IOCTL_NR(cmd);
    const struct drm_ioctl_desc *desc;
    long ret;
    int idx;

    if (DRM_IOCTL_TYPE(cmd) != DRM_IOCTL_BASE || nr >= ARRAY_SIZE(lc_drm_core_ioctls))
        return -ENOTTY;
    desc = &lc_drm_core_ioctls[nr];
    if (!desc->func || _IOC_SIZE(desc->cmd) != _IOC_SIZE(cmd))
        return -EINVAL;
    if (!drm_dev_enter(dev, &idx))
        return -ENODEV;
    ret = desc->func(dev, data, file);
    drm_dev_exit(idx);
    return ret;
}

static bool lc_drm_file_owns(struct drm_file *file, struct drm_gem_object *obj)
{
    unsigned long index;
    void *entry;

    xa_for_each(&file->object_idr, index, entry)
    {
        if (entry == obj)
            return true;
    }
    return false;
}

int lc_drm_mmap(struct drm_file *file, u64 offset, u64 size, u64 *user_address)
{
    struct drm_device *dev = file->dev;
    struct lc_drm_mapping *mapping;
    struct drm_gem_object *obj;
    int err;

    if (!size || (offset | size) & ~PAGE_MASK)
        return -EINVAL;

    xa_lock(&dev->lc_mmap_offsets);
    obj = xa_load(&dev->lc_mmap_offsets, (unsigned long)(offset >> PAGE_SHIFT));
    if (obj)
        drm_gem_object_get(obj);
    xa_unlock(&dev->lc_mmap_offsets);
    if (!obj)
        return -EINVAL;
    if (size > obj->size || !lc_drm_file_owns(file, obj) || !obj->funcs->mmap)
    {
        drm_gem_object_put(obj);
        return -EACCES;
    }

    mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
    if (!mapping)
    {
        drm_gem_object_put(obj);
        return -ENOMEM;
    }
    mapping->obj = obj;
    mapping->vma.vm_end = size;
    mapping->vma.vm_pgoff = offset >> PAGE_SHIFT;
    mapping->vma.vm_flags = VM_READ | VM_WRITE | VM_SHARED;
    mapping->vma.vm_private_data = obj;
    err = obj->funcs->mmap(obj, &mapping->vma);
    if (err)
    {
        kfree(mapping);
        drm_gem_object_put(obj);
        return err;
    }

    mutex_lock(&file->lc_mappings_lock);
    list_add(&mapping->node, &file->lc_mappings);
    mutex_unlock(&file->lc_mappings_lock);
    *user_address = mapping->vma.vm_start;
    return 0;
}

int lc_drm_munmap(struct drm_file *file, u64 user_address)
{
    struct lc_drm_mapping *mapping, *found = NULL;

    mutex_lock(&file->lc_mappings_lock);
    list_for_each_entry(mapping, &file->lc_mappings, node)
    {
        if (mapping->vma.vm_start == user_address)
        {
            list_del(&mapping->node);
            found = mapping;
            break;
        }
    }
    mutex_unlock(&file->lc_mappings_lock);
    if (!found)
        return -EINVAL;
    lc_drm_unmap(file, found);
    return 0;
}

int lc_drm_printk(const char *level, const struct drm_device *drm, const char *fmt, ...)
{
    char format[320];
    va_list args;
    int r;

    lc_scnprintf(format, sizeof(format), "%s[drm] %s: %s", level, drm && drm->driver ? drm->driver->name : "drm", fmt);
    va_start(args, fmt);
    r = lc_vprintk(format, args);
    va_end(args);
    return r;
}

void drm_printf(struct drm_printer *p, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    if (p && p->printfn)
        p->printfn(p, fmt, args);
    va_end(args);
}

static void lc_drm_printer_log(struct drm_printer *p, const char *fmt, va_list args)
{
    char buffer[512];

    lc_vscnprintf(buffer, sizeof(buffer), fmt, args);
    lc_printk(KERN_INFO "%s%s", p->prefix ? p->prefix : "", buffer);
}

struct drm_printer drm_info_printer(struct device *dev)
{
    struct drm_printer p = { lc_drm_printer_log, dev, NULL };

    return p;
}

struct drm_printer drm_err_printer(struct drm_device *drm, const char *prefix)
{
    struct drm_printer p = { lc_drm_printer_log, drm, prefix };

    return p;
}
