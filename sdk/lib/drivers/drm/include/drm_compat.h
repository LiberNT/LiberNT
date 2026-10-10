/*
 * PROJECT:     LiberNT DRM core library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DRM device, file, GEM, shmem, range allocator, ioctl and print interfaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <linux_compat.h>
#include <drm/drm.h>

struct drm_device;
struct drm_file;
struct drm_gem_object;
struct drm_printer;
struct drm_minor;

enum drm_driver_feature
{
    DRIVER_GEM = 1U << 0,
    DRIVER_MODESET = 1U << 1,
    DRIVER_RENDER = 1U << 3,
    DRIVER_ATOMIC = 1U << 4,
    DRIVER_SYNCOBJ = 1U << 5,
    DRIVER_SYNCOBJ_TIMELINE = 1U << 6,
    DRIVER_COMPUTE_ACCEL = 1U << 7,
    DRIVER_GEM_GPUVA = 1U << 8,
    DRIVER_CURSOR_HOTSPOT = 1U << 9,
};

enum drm_ioctl_flags
{
    DRM_AUTH = 1,
    DRM_MASTER = 2,
    DRM_ROOT_ONLY = 4,
    DRM_RENDER_ALLOW = 32,
};

#define DRM_IOCTL_NR(n) _IOC_NR(n)
#define DRM_IOCTL_TYPE(n) _IOC_TYPE(n)
#define DRM_MAJOR 226

typedef int drm_ioctl_t(struct drm_device *dev, void *data, struct drm_file *file_priv);

struct drm_ioctl_desc
{
    unsigned int cmd;
    enum drm_ioctl_flags flags;
    drm_ioctl_t *func;
    const char *name;
};

#define DRM_IOCTL_DEF_DRV(ioctl, _func, _flags) \
    [DRM_IOCTL_NR(DRM_IOCTL_##ioctl) - DRM_COMMAND_BASE] = { \
        .cmd = DRM_IOCTL_##ioctl, .flags = _flags, .func = _func, .name = #ioctl }

struct sg_table;
struct drm_driver
{
    u32 driver_features;
    int (*open)(struct drm_device *dev, struct drm_file *file);
    void (*postclose)(struct drm_device *dev, struct drm_file *file);
    void (*release)(struct drm_device *dev);
    const struct drm_ioctl_desc *ioctls;
    int num_ioctls;
    const struct file_operations *fops;
    void (*debugfs_init)(struct drm_minor *minor);
    struct drm_gem_object *(*gem_create_object)(struct drm_device *dev, size_t size);
    struct drm_gem_object *(*gem_prime_import_sg_table)(struct drm_device *dev, struct dma_buf_attachment *attach,
                                                        struct sg_table *sgt);
    const char *name;
    const char *desc;
    const char *date;
    int major;
    int minor;
    int patchlevel;
};

struct drm_minor
{
    int index;
    struct drm_device *dev;
    struct dentry *debugfs_root;
};

struct drm_device
{
    struct kref ref;
    struct device *dev;
    const struct drm_driver *driver;
    void *dev_private;
    struct drm_minor *render;
    struct drm_minor *primary;
    bool registered;
    bool unplugged;
    atomic_t lc_inflight;
    wait_queue_head_t lc_inflight_wq;
    struct list_head lc_managed;
    spinlock_t lc_managed_lock;
    struct mutex filelist_mutex;
    struct list_head filelist;
    struct xarray lc_mmap_offsets;
    u64 lc_next_mmap_page;
    void *lc_allocation;
};

struct drm_file
{
    struct drm_device *dev;
    void *driver_priv;
    struct file *filp;
    u64 client_id;
    bool authenticated;
    bool was_master;
    bool is_master;
    struct xarray object_idr;
    spinlock_t table_lock;
    struct xarray syncobj_xa;
    struct list_head lhead;
    struct list_head lc_mappings;
    struct mutex lc_mappings_lock;
    pid_t lc_pid;
};

struct drm_vma_offset_node
{
    u64 start;
    u64 pages;
};
static inline u64 drm_vma_node_offset_addr(struct drm_vma_offset_node *node) { return node->start << PAGE_SHIFT; }
static inline bool drm_vma_node_has_offset(struct drm_vma_offset_node *node) { return node->start != 0; }

struct vm_operations_struct
{
    void (*open)(struct vm_area_struct *area);
    void (*close)(struct vm_area_struct *area);
};

struct drm_gem_object_funcs
{
    void (*free)(struct drm_gem_object *obj);
    int (*open)(struct drm_gem_object *obj, struct drm_file *file);
    void (*close)(struct drm_gem_object *obj, struct drm_file *file);
    void (*print_info)(struct drm_printer *p, unsigned int indent, const struct drm_gem_object *obj);
    struct dma_buf *(*export)(struct drm_gem_object *obj, int flags);
    int (*pin)(struct drm_gem_object *obj);
    void (*unpin)(struct drm_gem_object *obj);
    struct sg_table *(*get_sg_table)(struct drm_gem_object *obj);
    int (*vmap)(struct drm_gem_object *obj, struct iosys_map *map);
    void (*vunmap)(struct drm_gem_object *obj, struct iosys_map *map);
    int (*mmap)(struct drm_gem_object *obj, struct vm_area_struct *vma);
    int (*evict)(struct drm_gem_object *obj);
    const struct vm_operations_struct *vm_ops;
};

struct drm_gem_object
{
    struct kref refcount;
    unsigned int handle_count;
    struct drm_device *dev;
    struct file *filp;
    struct drm_vma_offset_node vma_node;
    size_t size;
    int name;
    struct dma_buf *dma_buf;
    struct dma_buf_attachment *import_attach;
    struct dma_resv *resv;
    struct dma_resv _resv;
    struct
    {
        struct list_head list;
        struct mutex lock;
    } gpuva;
    const struct drm_gem_object_funcs *funcs;
};

#define drm_gem_gpuva_set_lock(obj, lock) do { (void)(obj); (void)(lock); } while (0)
#define drm_gem_gpuva_assert_lock_held(gpuvm, obj) do { (void)(gpuvm); (void)(obj); } while (0)
static inline void drm_gem_gpuva_init(struct drm_gem_object *obj) { INIT_LIST_HEAD(&obj->gpuva.list); }
#define drm_gem_for_each_gpuvm_bo(entry__, obj__) list_for_each_entry(entry__, &(obj__)->gpuva.list, list.entry.gem)
#define drm_gem_for_each_gpuvm_bo_safe(entry__, next__, obj__) \
    list_for_each_entry_safe(entry__, next__, &(obj__)->gpuva.list, list.entry.gem)

int drm_gem_object_init(struct drm_device *dev, struct drm_gem_object *obj, size_t size);
void drm_gem_private_object_init(struct drm_device *dev, struct drm_gem_object *obj, size_t size);
void drm_gem_private_object_fini(struct drm_gem_object *obj);
void drm_gem_object_release(struct drm_gem_object *obj);
void drm_gem_object_free(struct kref *kref);
static inline void drm_gem_object_get(struct drm_gem_object *obj) { kref_get(&obj->refcount); }
static inline void drm_gem_object_put(struct drm_gem_object *obj)
{
    if (obj)
        kref_put(&obj->refcount, drm_gem_object_free);
}
struct drm_gem_object *drm_gem_object_lookup(struct drm_file *filp, u32 handle);
int drm_gem_handle_create(struct drm_file *file_priv, struct drm_gem_object *obj, u32 *handlep);
int drm_gem_handle_delete(struct drm_file *filp, u32 handle);
int drm_gem_create_mmap_offset(struct drm_gem_object *obj);
void drm_gem_free_mmap_offset(struct drm_gem_object *obj);
int drm_gem_lock_reservations(struct drm_gem_object **objs, int count, struct ww_acquire_ctx *acquire_ctx);
void drm_gem_unlock_reservations(struct drm_gem_object **objs, int count, struct ww_acquire_ctx *acquire_ctx);
static inline struct dma_buf *drm_gem_prime_export(struct drm_gem_object *obj, int flags)
{
    (void)obj;
    (void)flags;
    return ERR_PTR(-EOPNOTSUPP);
}
static inline bool drm_gem_is_imported(const struct drm_gem_object *obj) { return obj->import_attach != NULL; }
#define DEFINE_DRM_GEM_FOPS(name) static const struct file_operations name = { 0 }

struct drm_gem_shmem_object
{
    struct drm_gem_object base;
    struct page **pages;
    void *lc_pages_allocation;
    u64 *lc_pfns;
    refcount_t pages_use_count;
    refcount_t pages_pin_count;
    int madv;
    struct sg_table *sgt;
    void *vaddr;
    void *lc_vmap_cookie;
    refcount_t vmap_use_count;
    bool pages_mark_dirty_on_put;
    bool pages_mark_accessed_on_put;
    bool map_wc;
    void (*lc_import_release)(void *context);
    void *lc_import_context;
};
#define to_drm_gem_shmem_obj(obj) container_of(obj, struct drm_gem_shmem_object, base)
extern const struct vm_operations_struct drm_gem_shmem_vm_ops;
struct drm_gem_shmem_object *drm_gem_shmem_create(struct drm_device *dev, size_t size);
struct drm_gem_shmem_object *drm_gem_shmem_lc_import(struct drm_device *dev, u64 first_pfn, size_t size,
                                                      void (*release)(void *context), void *context);
void drm_gem_shmem_free(struct drm_gem_shmem_object *shmem);
int drm_gem_shmem_pin(struct drm_gem_shmem_object *shmem);
void drm_gem_shmem_unpin(struct drm_gem_shmem_object *shmem);
struct sg_table *drm_gem_shmem_get_sg_table(struct drm_gem_shmem_object *shmem);
struct sg_table *drm_gem_shmem_get_pages_sgt(struct drm_gem_shmem_object *shmem);
int drm_gem_shmem_vmap_locked(struct drm_gem_shmem_object *shmem, struct iosys_map *map);
void drm_gem_shmem_vunmap_locked(struct drm_gem_shmem_object *shmem, struct iosys_map *map);
int drm_gem_shmem_mmap(struct drm_gem_shmem_object *shmem, struct vm_area_struct *vma);
void drm_gem_shmem_print_info(const struct drm_gem_shmem_object *shmem, struct drm_printer *p, unsigned int indent);
static inline void drm_gem_shmem_object_free(struct drm_gem_object *obj) { drm_gem_shmem_free(to_drm_gem_shmem_obj(obj)); }
static inline void drm_gem_shmem_object_print_info(struct drm_printer *p, unsigned int indent, const struct drm_gem_object *obj)
{
    drm_gem_shmem_print_info(container_of(obj, const struct drm_gem_shmem_object, base), p, indent);
}
static inline int drm_gem_shmem_object_pin(struct drm_gem_object *obj) { return drm_gem_shmem_pin(to_drm_gem_shmem_obj(obj)); }
static inline void drm_gem_shmem_object_unpin(struct drm_gem_object *obj) { drm_gem_shmem_unpin(to_drm_gem_shmem_obj(obj)); }
static inline struct sg_table *drm_gem_shmem_object_get_sg_table(struct drm_gem_object *obj)
{
    return drm_gem_shmem_get_sg_table(to_drm_gem_shmem_obj(obj));
}
static inline int drm_gem_shmem_object_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{
    return drm_gem_shmem_vmap_locked(to_drm_gem_shmem_obj(obj), map);
}
static inline void drm_gem_shmem_object_vunmap(struct drm_gem_object *obj, struct iosys_map *map)
{
    drm_gem_shmem_vunmap_locked(to_drm_gem_shmem_obj(obj), map);
}
static inline int drm_gem_shmem_object_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
    return drm_gem_shmem_mmap(to_drm_gem_shmem_obj(obj), vma);
}
static inline struct drm_gem_object *drm_gem_shmem_prime_import_sg_table(struct drm_device *dev, struct dma_buf_attachment *attach,
                                                                         struct sg_table *sgt)
{
    (void)dev;
    (void)attach;
    (void)sgt;
    return ERR_PTR(-EOPNOTSUPP);
}

struct drm_mm_node
{
    u64 start;
    u64 size;
    unsigned long color;
    struct list_head node_list;
    struct drm_mm *mm;
    bool allocated;
};
struct drm_mm
{
    u64 start;
    u64 size;
    struct list_head nodes;
};
enum drm_mm_insert_mode
{
    DRM_MM_INSERT_BEST = 0,
    DRM_MM_INSERT_LOW,
    DRM_MM_INSERT_HIGH,
    DRM_MM_INSERT_EVICT,
    DRM_MM_INSERT_ONCE = 0x80000000,
};
void drm_mm_init(struct drm_mm *mm, u64 start, u64 size);
void drm_mm_takedown(struct drm_mm *mm);
int drm_mm_insert_node_in_range(struct drm_mm *mm, struct drm_mm_node *node, u64 size, u64 alignment, unsigned long color,
                                u64 range_start, u64 range_end, enum drm_mm_insert_mode mode);
static inline int drm_mm_insert_node_generic(struct drm_mm *mm, struct drm_mm_node *node, u64 size, u64 alignment,
                                             unsigned long color, enum drm_mm_insert_mode mode)
{
    return drm_mm_insert_node_in_range(mm, node, size, alignment, color, 0, U64_MAX, mode);
}
static inline int drm_mm_insert_node(struct drm_mm *mm, struct drm_mm_node *node, u64 size)
{
    return drm_mm_insert_node_generic(mm, node, size, 0, 0, DRM_MM_INSERT_BEST);
}
int drm_mm_reserve_node(struct drm_mm *mm, struct drm_mm_node *node);
void drm_mm_remove_node(struct drm_mm_node *node);
static inline bool drm_mm_node_allocated(const struct drm_mm_node *node) { return node->allocated; }
static inline bool drm_mm_clean(const struct drm_mm *mm) { return list_empty(&mm->nodes); }

struct drm_printer
{
    void (*printfn)(struct drm_printer *p, const char *fmt, va_list args);
    void *arg;
    const char *prefix;
};
void drm_printf(struct drm_printer *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
struct drm_printer drm_info_printer(struct device *dev);
struct drm_printer drm_err_printer(struct drm_device *drm, const char *prefix);
int lc_drm_printk(const char *level, const struct drm_device *drm, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define drm_err(drm, fmt, ...) lc_drm_printk(KERN_ERR, drm, fmt, ##__VA_ARGS__)
#define drm_warn(drm, fmt, ...) lc_drm_printk(KERN_WARNING, drm, fmt, ##__VA_ARGS__)
#define drm_notice(drm, fmt, ...) lc_drm_printk(KERN_NOTICE, drm, fmt, ##__VA_ARGS__)
#define drm_info(drm, fmt, ...) lc_drm_printk(KERN_INFO, drm, fmt, ##__VA_ARGS__)
#define drm_dbg(drm, fmt, ...) do { if (0) lc_drm_printk(KERN_DEBUG, drm, fmt, ##__VA_ARGS__); } while (0)
#define drm_dbg_driver drm_dbg
#define drm_dbg_core drm_dbg
#define drm_dbg_kms drm_dbg
#define drm_dbg_prime drm_dbg
#define drm_err_once drm_err
#define drm_warn_once drm_warn
#define drm_info_once drm_info
#define drm_err_ratelimited drm_err
#define drm_warn_ratelimited drm_warn
#define drm_WARN(drm, cond, fmt, ...) WARN(cond, fmt, ##__VA_ARGS__)
#define drm_WARN_ONCE(drm, cond, fmt, ...) WARN_ONCE(cond, fmt, ##__VA_ARGS__)
#define drm_WARN_ON(drm, x) WARN_ON(x)
#define drm_WARN_ON_ONCE(drm, x) WARN_ON_ONCE(x)
#define DRM_ERROR(fmt, ...) lc_printk(KERN_ERR "[drm] " fmt, ##__VA_ARGS__)
#define DRM_WARN(fmt, ...) lc_printk(KERN_WARNING "[drm] " fmt, ##__VA_ARGS__)
#define DRM_NOTE(fmt, ...) lc_printk(KERN_NOTICE "[drm] " fmt, ##__VA_ARGS__)
#define DRM_INFO(fmt, ...) lc_printk(KERN_INFO "[drm] " fmt, ##__VA_ARGS__)
#define DRM_DEBUG(fmt, ...) do { } while (0)
#define DRM_DEBUG_DRIVER(fmt, ...) do { } while (0)
#define DRM_DEV_ERROR(dev, fmt, ...) dev_err(dev, fmt, ##__VA_ARGS__)

struct drm_device *__devm_drm_dev_alloc(struct device *parent, const struct drm_driver *driver, size_t size, size_t offset);
#define devm_drm_dev_alloc(parent, driver, type, member) \
    ((type *)__devm_drm_dev_alloc(parent, driver, sizeof(type), offsetof(type, member)))
int drm_dev_register(struct drm_device *dev, unsigned long flags);
void drm_dev_unregister(struct drm_device *dev);
void drm_dev_unplug(struct drm_device *dev);
void drm_dev_get(struct drm_device *dev);
void drm_dev_put(struct drm_device *dev);
bool drm_dev_enter(struct drm_device *dev, int *idx);
void drm_dev_exit(int idx);
static inline bool drm_dev_is_unplugged(struct drm_device *dev) { return READ_ONCE(dev->unplugged); }
static inline bool drm_core_check_feature(const struct drm_device *dev, u32 feature) { return (dev->driver->driver_features & feature) != 0; }
static inline bool drm_is_current_master(struct drm_file *fpriv) { (void)fpriv; return false; }
static inline bool drm_is_render_client(const struct drm_file *file_priv) { (void)file_priv; return true; }
signed long drm_timeout_abs_to_jiffies(int64_t timeout_nsec);

void *drmm_kmalloc(struct drm_device *dev, size_t size, gfp_t gfp);
#define drmm_kzalloc(d, s, g) drmm_kmalloc(d, s, (g) | __GFP_ZERO)
#define drmm_kcalloc(d, n, s, g) drmm_kmalloc(d, size_mul(n, s), (g) | __GFP_ZERO)
void drmm_kfree(struct drm_device *dev, void *data);
int drmm_add_action(struct drm_device *dev, void (*action)(struct drm_device *, void *), void *data);
int drmm_add_action_or_reset(struct drm_device *dev, void (*action)(struct drm_device *, void *), void *data);
int drmm_mutex_init(struct drm_device *dev, struct mutex *lock);

struct drm_file *lc_drm_file_open(struct drm_device *dev);
void lc_drm_file_close(struct drm_file *file);
long lc_drm_ioctl(struct drm_file *file, unsigned int cmd, void __user *arg, unsigned int arg_size);
long lc_drm_ioctl_kernel(struct drm_file *file, unsigned int cmd, void *data);
int lc_drm_mmap(struct drm_file *file, u64 offset, u64 size, u64 *user_address);
int lc_drm_munmap(struct drm_file *file, u64 user_address);

void drm_syncobj_open(struct drm_file *file_private);
void drm_syncobj_release(struct drm_file *file_private);
int drm_syncobj_create_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_destroy_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_handle_to_fd_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_fd_to_handle_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_transfer_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_wait_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_timeline_wait_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_eventfd_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_reset_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_signal_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_timeline_signal_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);
int drm_syncobj_query_ioctl(struct drm_device *dev, void *data, struct drm_file *file_private);

#define trace_drm_sched_job_add_dep(...) do { } while (0)
#define trace_drm_sched_job_add_dep_enabled() 0
#define trace_drm_sched_job_done(...) do { } while (0)
#define trace_drm_sched_job_queue(...) do { } while (0)
#define trace_drm_sched_job_run(...) do { } while (0)
#define trace_drm_sched_job_unschedulable(...) do { } while (0)
#define trace_drm_sched_job_unschedulable_enabled() 0
