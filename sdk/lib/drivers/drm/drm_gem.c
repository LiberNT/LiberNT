/*
 * PROJECT:     LiberNT DRM core library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GEM objects and handles, page-backed GEM objects and the range allocator
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <drm_compat.h>

const struct vm_operations_struct drm_gem_shmem_vm_ops = { 0 };

void drm_gem_private_object_init(struct drm_device *dev, struct drm_gem_object *obj, size_t size)
{
    kref_init(&obj->refcount);
    obj->handle_count = 0;
    obj->dev = dev;
    obj->filp = NULL;
    obj->size = size;
    obj->resv = &obj->_resv;
    dma_resv_init(&obj->_resv);
    INIT_LIST_HEAD(&obj->gpuva.list);
    mutex_init(&obj->gpuva.lock);
    obj->vma_node.start = 0;
    obj->vma_node.pages = 0;
}

int drm_gem_object_init(struct drm_device *dev, struct drm_gem_object *obj, size_t size)
{
    drm_gem_private_object_init(dev, obj, size);
    return 0;
}

void drm_gem_private_object_fini(struct drm_gem_object *obj)
{
    dma_resv_fini(&obj->_resv);
}

void drm_gem_free_mmap_offset(struct drm_gem_object *obj)
{
    if (!obj->vma_node.start)
        return;
    xa_erase(&obj->dev->lc_mmap_offsets, (unsigned long)obj->vma_node.start);
    obj->vma_node.start = 0;
}

void drm_gem_object_release(struct drm_gem_object *obj)
{
    drm_gem_free_mmap_offset(obj);
    drm_gem_private_object_fini(obj);
}

void drm_gem_object_free(struct kref *kref)
{
    struct drm_gem_object *obj = container_of(kref, struct drm_gem_object, refcount);

    if (WARN_ON(!obj->funcs || !obj->funcs->free))
        return;
    obj->funcs->free(obj);
}

int drm_gem_create_mmap_offset(struct drm_gem_object *obj)
{
    struct drm_device *dev = obj->dev;
    u64 start;
    void *old;

    if (obj->vma_node.start)
        return 0;
    xa_lock(&dev->lc_mmap_offsets);
    start = dev->lc_next_mmap_page;
    dev->lc_next_mmap_page += (obj->size >> PAGE_SHIFT) + 1;
    old = __xa_store(&dev->lc_mmap_offsets, (unsigned long)start, obj, GFP_KERNEL);
    xa_unlock(&dev->lc_mmap_offsets);
    if (xa_is_err(old))
        return xa_err(old);
    obj->vma_node.start = start;
    obj->vma_node.pages = obj->size >> PAGE_SHIFT;
    return 0;
}

int drm_gem_handle_create(struct drm_file *file_priv, struct drm_gem_object *obj, u32 *handlep)
{
    u32 handle;
    int err;

    drm_gem_object_get(obj);
    err = xa_alloc(&file_priv->object_idr, &handle, obj, xa_limit_32b, GFP_KERNEL);
    if (err)
    {
        drm_gem_object_put(obj);
        return err;
    }
    obj->handle_count++;
    if (obj->funcs->open)
    {
        err = obj->funcs->open(obj, file_priv);
        if (err)
        {
            xa_erase(&file_priv->object_idr, handle);
            obj->handle_count--;
            drm_gem_object_put(obj);
            return err;
        }
    }
    *handlep = handle;
    return 0;
}

struct drm_gem_object *drm_gem_object_lookup(struct drm_file *filp, u32 handle)
{
    struct drm_gem_object *obj;

    xa_lock(&filp->object_idr);
    obj = xa_load(&filp->object_idr, handle);
    if (obj)
        drm_gem_object_get(obj);
    xa_unlock(&filp->object_idr);
    return obj;
}

static void drm_gem_release_handle(struct drm_file *file, struct drm_gem_object *obj)
{
    if (obj->funcs->close)
        obj->funcs->close(obj, file);
    obj->handle_count--;
    drm_gem_object_put(obj);
}

int drm_gem_handle_delete(struct drm_file *filp, u32 handle)
{
    struct drm_gem_object *obj = xa_erase(&filp->object_idr, handle);

    if (!obj)
        return -EINVAL;
    drm_gem_release_handle(filp, obj);
    return 0;
}

int drm_gem_close_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
    struct drm_gem_close *args = data;

    (void)dev;
    return drm_gem_handle_delete(file, args->handle);
}

void drm_gem_release(struct drm_device *dev, struct drm_file *file)
{
    unsigned long index;
    void *entry;

    (void)dev;
    xa_for_each(&file->object_idr, index, entry)
    {
        xa_erase(&file->object_idr, index);
        drm_gem_release_handle(file, entry);
    }
}

int drm_gem_lock_reservations(struct drm_gem_object **objs, int count, struct ww_acquire_ctx *acquire_ctx)
{
    int i, err;

    ww_acquire_init(acquire_ctx, &reservation_ww_class);
    for (i = 0; i < count; ++i)
    {
        err = dma_resv_lock(objs[i]->resv, acquire_ctx);
        if (err && err != -EALREADY)
        {
            while (--i >= 0)
                dma_resv_unlock(objs[i]->resv);
            ww_acquire_fini(acquire_ctx);
            return err;
        }
    }
    ww_acquire_done(acquire_ctx);
    return 0;
}

void drm_gem_unlock_reservations(struct drm_gem_object **objs, int count, struct ww_acquire_ctx *acquire_ctx)
{
    int i;

    for (i = 0; i < count; ++i)
        dma_resv_unlock(objs[i]->resv);
    ww_acquire_fini(acquire_ctx);
}

struct drm_gem_shmem_object *drm_gem_shmem_create(struct drm_device *dev, size_t size)
{
    struct drm_gem_shmem_object *shmem;
    struct drm_gem_object *obj;

    size = PAGE_ALIGN(size);
    if (!size)
        return ERR_PTR(-EINVAL);
    if (dev->driver->gem_create_object)
    {
        obj = dev->driver->gem_create_object(dev, size);
        if (IS_ERR(obj))
            return ERR_CAST(obj);
        shmem = to_drm_gem_shmem_obj(obj);
    }
    else
    {
        shmem = kzalloc(sizeof(*shmem), GFP_KERNEL);
        if (!shmem)
            return ERR_PTR(-ENOMEM);
        obj = &shmem->base;
    }
    drm_gem_object_init(dev, obj, size);
    refcount_set(&shmem->pages_use_count, 0);
    refcount_set(&shmem->pages_pin_count, 0);
    refcount_set(&shmem->vmap_use_count, 0);
    return shmem;
}

struct drm_gem_shmem_object *drm_gem_shmem_lc_import(struct drm_device *dev, u64 first_pfn, size_t size,
                                                      void (*release)(void *context), void *context)
{
    struct drm_gem_shmem_object *shmem;
    struct page *pages;
    size_t count, i;

    if (!size || (size & (PAGE_SIZE - 1)) || !release)
        return ERR_PTR(-EINVAL);
    shmem = drm_gem_shmem_create(dev, size);
    if (IS_ERR(shmem))
        return shmem;
    count = size >> PAGE_SHIFT;
    shmem->lc_pfns = kvmalloc_array(count, sizeof(*shmem->lc_pfns), GFP_KERNEL);
    shmem->pages = kvmalloc_array(count, sizeof(*shmem->pages), GFP_KERNEL);
    pages = kvzalloc(count * sizeof(*pages), GFP_KERNEL);
    if (!shmem->lc_pfns || !shmem->pages || !pages)
    {
        kvfree(pages);
        kvfree(shmem->pages);
        kvfree(shmem->lc_pfns);
        shmem->pages = NULL;
        shmem->lc_pfns = NULL;
        drm_gem_shmem_free(shmem);
        return ERR_PTR(-ENOMEM);
    }
    for (i = 0; i < count; ++i)
    {
        shmem->lc_pfns[i] = first_pfn + i;
        pages[i].pfn = first_pfn + i;
        shmem->pages[i] = &pages[i];
    }
    shmem->map_wc = true;
    shmem->lc_import_release = release;
    shmem->lc_import_context = context;
    return shmem;
}

static int drm_gem_shmem_get_pages(struct drm_gem_shmem_object *shmem)
{
    size_t count = shmem->base.size >> PAGE_SHIFT, i;
    struct page *pages;

    if (shmem->pages)
        return 0;
    shmem->lc_pfns = kvmalloc_array(count, sizeof(*shmem->lc_pfns), GFP_KERNEL);
    shmem->pages = kvmalloc_array(count, sizeof(*shmem->pages), GFP_KERNEL);
    pages = kvzalloc(count * sizeof(*pages), GFP_KERNEL);
    if (!shmem->lc_pfns || !shmem->pages || !pages)
        goto fail;
    shmem->lc_pages_allocation = lc_nt_alloc_pages(shmem->base.size, lc_get_page_allocation_limit(), LC_NT_CACHE_WRITECOMBINED,
                                                    shmem->lc_pfns, count);
    if (!shmem->lc_pages_allocation)
        goto fail;
    for (i = 0; i < count; ++i)
    {
        pages[i].pfn = shmem->lc_pfns[i];
        shmem->pages[i] = &pages[i];
    }
    return 0;

fail:
    kvfree(pages);
    kvfree(shmem->pages);
    kvfree(shmem->lc_pfns);
    shmem->pages = NULL;
    shmem->lc_pfns = NULL;
    return -ENOMEM;
}

static struct sg_table *drm_gem_shmem_build_sgt(struct drm_gem_shmem_object *shmem)
{
    size_t count = shmem->base.size >> PAGE_SHIFT, i;
    unsigned int nents = 0, index = 0;
    struct sg_table *sgt;
    int err;

    for (i = 0; i < count; ++i)
    {
        if (!i || shmem->lc_pfns[i] != shmem->lc_pfns[i - 1] + 1)
            nents++;
    }
    sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
    if (!sgt)
        return ERR_PTR(-ENOMEM);
    err = sg_alloc_table(sgt, nents, GFP_KERNEL);
    if (err)
    {
        kfree(sgt);
        return ERR_PTR(err);
    }
    for (i = 0; i < count; ++i)
    {
        if (i && shmem->lc_pfns[i] == shmem->lc_pfns[i - 1] + 1)
        {
            sgt->sgl[index - 1].length += PAGE_SIZE;
            continue;
        }
        sg_set_page(&sgt->sgl[index++], shmem->pages[i], PAGE_SIZE, 0);
    }
    dma_map_sgtable(shmem->base.dev->dev, sgt, DMA_BIDIRECTIONAL, 0);
    return sgt;
}

struct sg_table *drm_gem_shmem_get_pages_sgt(struct drm_gem_shmem_object *shmem)
{
    struct sg_table *sgt;
    int err;

    dma_resv_lock(shmem->base.resv, NULL);
    if (shmem->sgt)
    {
        sgt = shmem->sgt;
        goto out;
    }
    err = drm_gem_shmem_get_pages(shmem);
    if (err)
    {
        sgt = ERR_PTR(err);
        goto out;
    }
    sgt = drm_gem_shmem_build_sgt(shmem);
    if (!IS_ERR(sgt))
        shmem->sgt = sgt;
out:
    dma_resv_unlock(shmem->base.resv);
    return sgt;
}

struct sg_table *drm_gem_shmem_get_sg_table(struct drm_gem_shmem_object *shmem)
{
    int err = drm_gem_shmem_get_pages(shmem);

    if (err)
        return ERR_PTR(err);
    return drm_gem_shmem_build_sgt(shmem);
}

int drm_gem_shmem_pin(struct drm_gem_shmem_object *shmem)
{
    int err;

    dma_resv_lock(shmem->base.resv, NULL);
    err = drm_gem_shmem_get_pages(shmem);
    if (!err)
        refcount_inc(&shmem->pages_pin_count);
    dma_resv_unlock(shmem->base.resv);
    return err;
}

void drm_gem_shmem_unpin(struct drm_gem_shmem_object *shmem)
{
    if (refcount_read(&shmem->pages_pin_count))
        refcount_dec_and_test(&shmem->pages_pin_count);
}

int drm_gem_shmem_vmap_locked(struct drm_gem_shmem_object *shmem, struct iosys_map *map)
{
    int err;

    if (shmem->vaddr)
    {
        refcount_inc(&shmem->vmap_use_count);
        iosys_map_set_vaddr(map, shmem->vaddr);
        return 0;
    }
    err = drm_gem_shmem_get_pages(shmem);
    if (err)
        return err;
    shmem->vaddr = lc_nt_map_pfns(shmem->lc_pfns, shmem->base.size >> PAGE_SHIFT, LC_NT_CACHE_WRITECOMBINED, 0,
                                  &shmem->lc_vmap_cookie);
    if (!shmem->vaddr)
        return -ENOMEM;
    refcount_set(&shmem->vmap_use_count, 1);
    iosys_map_set_vaddr(map, shmem->vaddr);
    return 0;
}

void drm_gem_shmem_vunmap_locked(struct drm_gem_shmem_object *shmem, struct iosys_map *map)
{
    (void)map;
    if (WARN_ON(!shmem->vaddr))
        return;
    if (!refcount_dec_and_test(&shmem->vmap_use_count))
        return;
    lc_nt_unmap_pfns(shmem->vaddr, shmem->lc_vmap_cookie);
    shmem->vaddr = NULL;
    shmem->lc_vmap_cookie = NULL;
}

int drm_gem_shmem_mmap(struct drm_gem_shmem_object *shmem, struct vm_area_struct *vma)
{
    size_t first = vma->vm_pgoff - shmem->base.vma_node.start;
    size_t count = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    void *address;
    int err;

    if (first + count > (shmem->base.size >> PAGE_SHIFT))
        return -EINVAL;
    dma_resv_lock(shmem->base.resv, NULL);
    err = drm_gem_shmem_get_pages(shmem);
    dma_resv_unlock(shmem->base.resv);
    if (err)
        return err;
    address = lc_nt_map_pfns(&shmem->lc_pfns[first], count, LC_NT_CACHE_WRITECOMBINED, 1, &vma->lc_cookie);
    if (!address)
        return -ENOMEM;
    vma->vm_start = (uintptr_t)address;
    vma->vm_end = vma->vm_start + (count << PAGE_SHIFT);
    return 0;
}

void drm_gem_shmem_print_info(const struct drm_gem_shmem_object *shmem, struct drm_printer *p, unsigned int indent)
{
    (void)shmem;
    (void)p;
    (void)indent;
}

void drm_gem_shmem_free(struct drm_gem_shmem_object *shmem)
{
    struct page *pages = shmem->pages ? shmem->pages[0] : NULL;

    if (shmem->vaddr)
    {
        lc_nt_unmap_pfns(shmem->vaddr, shmem->lc_vmap_cookie);
        shmem->vaddr = NULL;
    }
    if (shmem->sgt)
    {
        sg_free_table(shmem->sgt);
        kfree(shmem->sgt);
        shmem->sgt = NULL;
    }
    if (shmem->lc_pages_allocation)
        lc_nt_free_pages(shmem->lc_pages_allocation);
    if (shmem->lc_import_release)
        shmem->lc_import_release(shmem->lc_import_context);
    kvfree(pages);
    kvfree(shmem->pages);
    kvfree(shmem->lc_pfns);
    drm_gem_object_release(&shmem->base);
    kfree(shmem);
}

void drm_mm_init(struct drm_mm *mm, u64 start, u64 size)
{
    mm->start = start;
    mm->size = size;
    INIT_LIST_HEAD(&mm->nodes);
}

void drm_mm_takedown(struct drm_mm *mm)
{
    WARN_ON(!list_empty(&mm->nodes));
}

static void drm_mm_link(struct drm_mm *mm, struct drm_mm_node *node)
{
    struct drm_mm_node *pos;

    list_for_each_entry(pos, &mm->nodes, node_list)
    {
        if (pos->start > node->start)
        {
            list_add_tail(&node->node_list, &pos->node_list);
            goto linked;
        }
    }
    list_add_tail(&node->node_list, &mm->nodes);
linked:
    node->mm = mm;
    node->allocated = true;
}

static bool drm_mm_fit(u64 hole_start, u64 hole_end, u64 size, u64 alignment, bool high, u64 *out)
{
    u64 start;

    if (hole_end <= hole_start || hole_end - hole_start < size)
        return false;
    if (high)
    {
        start = hole_end - size;
        if (alignment)
            start -= start % alignment;
        if (start < hole_start)
            return false;
    }
    else
    {
        start = hole_start;
        if (alignment && start % alignment)
            start += alignment - start % alignment;
        if (start < hole_start || start > hole_end || hole_end - start < size)
            return false;
    }
    *out = start;
    return true;
}

int drm_mm_insert_node_in_range(struct drm_mm *mm, struct drm_mm_node *node, u64 size, u64 alignment, unsigned long color,
                                u64 range_start, u64 range_end, enum drm_mm_insert_mode mode)
{
    bool high = (mode & ~DRM_MM_INSERT_ONCE) == DRM_MM_INSERT_HIGH;
    u64 lo = max(range_start, mm->start);
    u64 hi = min(range_end, mm->start + mm->size);
    u64 hole_start = lo, found = 0;
    struct drm_mm_node *pos;
    bool have = false;

    if (!size || lo >= hi)
        return -ENOSPC;

    list_for_each_entry(pos, &mm->nodes, node_list)
    {
        u64 candidate;

        if (pos->start + pos->size <= lo)
            continue;
        if (pos->start >= hi)
            break;
        if (drm_mm_fit(hole_start, min(pos->start, hi), size, alignment, high, &candidate))
        {
            found = candidate;
            have = true;
            if (!high)
                break;
        }
        hole_start = max(hole_start, pos->start + pos->size);
    }
    if ((!have || high) && hole_start < hi)
    {
        u64 candidate;

        if (drm_mm_fit(hole_start, hi, size, alignment, high, &candidate))
        {
            found = candidate;
            have = true;
        }
    }
    if (!have)
        return -ENOSPC;

    node->start = found;
    node->size = size;
    node->color = color;
    drm_mm_link(mm, node);
    return 0;
}

int drm_mm_reserve_node(struct drm_mm *mm, struct drm_mm_node *node)
{
    struct drm_mm_node *pos;
    u64 end;

    if (!node->size || check_add_overflow(node->start, node->size, &end))
        return -ENOSPC;
    if (node->start < mm->start || end > mm->start + mm->size)
        return -ENOSPC;
    list_for_each_entry(pos, &mm->nodes, node_list)
    {
        if (pos->start < end && node->start < pos->start + pos->size)
            return -ENOSPC;
    }
    drm_mm_link(mm, node);
    return 0;
}

void drm_mm_remove_node(struct drm_mm_node *node)
{
    if (WARN_ON(!node->allocated))
        return;
    list_del(&node->node_list);
    node->allocated = false;
    node->mm = NULL;
}
