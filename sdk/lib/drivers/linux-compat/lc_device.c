/*
 * PROJECT:     LiberNT Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux device model, managed resources, IRQs, clocks, runtime PM and firmware
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <linux_compat.h>

struct lc_devres
{
    struct list_head node;
    void (*action)(void *data);
    void *data;
    bool allocation;
};

struct clk
{
    struct list_head node;
    const char *id;
    const struct lc_clk_ops *ops;
    void *context;
    int enable_count;
};

struct lc_ioremap_record
{
    struct list_head node;
    void *address;
    size_t size;
};

struct lc_irq_line
{
    bool present;
    bool requested;
    u32 vector;
    u8 irql;
    bool level_sensitive;
    irq_handler_t handler;
    irq_handler_t thread_fn;
    void *dev_id;
    unsigned long flags;
    const char *name;
    struct workqueue_struct *thread_wq;
    struct work_struct thread_work;
    int disable_depth;
    int masked;
    spinlock_t lock;
};

static struct lc_irq_line lc_irq_lines[LC_IRQ_MAX];
static DEFINE_SPINLOCK(lc_ioremap_lock);
static LIST_HEAD(lc_ioremap_records);

void lc_device_initialize(struct device *dev, const char *name)
{
    dev->init_name = name;
    INIT_LIST_HEAD(&dev->lc_devres);
    spin_lock_init(&dev->lc_devres_lock);
    INIT_LIST_HEAD(&dev->lc_clks);
    mutex_init(&dev->lc_pm.lock);
    spin_lock_init(&dev->lc_pm.state_lock);
    dev->lc_pm.usage = 0;
    dev->lc_pm.active = false;
    dev->lc_pm.enabled = false;
    dev->lc_pm.error = false;
}

static int lc_devres_add(struct device *dev, void (*action)(void *), void *data, bool allocation)
{
    struct lc_devres *res = kzalloc(sizeof(*res), GFP_KERNEL);

    if (!res)
        return -ENOMEM;
    res->action = action;
    res->data = data;
    res->allocation = allocation;
    spin_lock(&dev->lc_devres_lock);
    list_add(&res->node, &dev->lc_devres);
    spin_unlock(&dev->lc_devres_lock);
    return 0;
}

int devm_add_action(struct device *dev, void (*action)(void *), void *data)
{
    return lc_devres_add(dev, action, data, false);
}

int devm_add_action_or_reset(struct device *dev, void (*action)(void *), void *data)
{
    int err = devm_add_action(dev, action, data);

    if (err)
        action(data);
    return err;
}

static struct lc_devres *lc_devres_take(struct device *dev, void (*action)(void *), void *data)
{
    struct lc_devres *res, *found = NULL;

    spin_lock(&dev->lc_devres_lock);
    list_for_each_entry(res, &dev->lc_devres, node)
    {
        if (res->action == action && res->data == data)
        {
            list_del(&res->node);
            found = res;
            break;
        }
    }
    spin_unlock(&dev->lc_devres_lock);
    return found;
}

void devm_remove_action(struct device *dev, void (*action)(void *), void *data)
{
    kfree(lc_devres_take(dev, action, data));
}

void devm_release_action(struct device *dev, void (*action)(void *), void *data)
{
    struct lc_devres *res = lc_devres_take(dev, action, data);

    if (res)
    {
        res->action(res->data);
        kfree(res);
    }
}

static void lc_devres_kfree(void *data)
{
    kfree(data);
}

void *devm_kmalloc(struct device *dev, size_t size, gfp_t gfp)
{
    void *p = kmalloc(size, gfp);

    if (p && lc_devres_add(dev, lc_devres_kfree, p, true))
    {
        kfree(p);
        return NULL;
    }
    return p;
}

void devm_kfree(struct device *dev, const void *p)
{
    struct lc_devres *res = lc_devres_take(dev, lc_devres_kfree, (void *)p);

    if (res)
        kfree(res);
    kfree(p);
}

void lc_device_release_resources(struct device *dev)
{
    for (;;)
    {
        struct lc_devres *res = NULL;

        spin_lock(&dev->lc_devres_lock);
        if (!list_empty(&dev->lc_devres))
        {
            res = list_first_entry(&dev->lc_devres, struct lc_devres, node);
            list_del(&res->node);
        }
        spin_unlock(&dev->lc_devres_lock);
        if (!res)
            break;
        res->action(res->data);
        kfree(res);
    }
}

struct resource *platform_get_resource(struct platform_device *pdev, unsigned int type, unsigned int num)
{
    u32 i;

    for (i = 0; i < pdev->num_resources; ++i)
    {
        struct resource *res = &pdev->resource[i];

        if ((res->flags & type) && num-- == 0)
            return res;
    }
    return NULL;
}

int platform_get_irq(struct platform_device *pdev, unsigned int num)
{
    struct resource *res = platform_get_resource(pdev, IORESOURCE_IRQ, num);

    return res ? (int)res->start : -ENXIO;
}

int platform_get_irq_byname(struct platform_device *pdev, const char *name)
{
    u32 i;

    for (i = 0; i < pdev->num_resources; ++i)
    {
        struct resource *res = &pdev->resource[i];

        if ((res->flags & IORESOURCE_IRQ) && res->name && !strcmp(res->name, name))
            return (int)res->start;
    }
    return -ENXIO;
}

void __iomem *lc_ioremap(phys_addr_t offset, size_t size)
{
    struct lc_ioremap_record *record = kzalloc(sizeof(*record), GFP_KERNEL);

    if (!record)
        return NULL;
    record->address = lc_nt_map_io(offset, size, LC_NT_CACHE_UNCACHED);
    if (!record->address)
    {
        kfree(record);
        return NULL;
    }
    record->size = size;
    spin_lock(&lc_ioremap_lock);
    list_add(&record->node, &lc_ioremap_records);
    spin_unlock(&lc_ioremap_lock);
    return record->address;
}

void lc_iounmap(volatile void __iomem *addr)
{
    struct lc_ioremap_record *record, *found = NULL;

    spin_lock(&lc_ioremap_lock);
    list_for_each_entry(record, &lc_ioremap_records, node)
    {
        if (record->address == (void *)addr)
        {
            list_del(&record->node);
            found = record;
            break;
        }
    }
    spin_unlock(&lc_ioremap_lock);
    if (!found)
        return;
    lc_nt_unmap_io(found->address, found->size);
    kfree(found);
}

static void lc_devm_iounmap(void *data)
{
    lc_iounmap(data);
}

void __iomem *devm_ioremap(struct device *dev, resource_size_t offset, resource_size_t size)
{
    void __iomem *address = lc_ioremap(offset, size);

    if (address && devm_add_action_or_reset(dev, lc_devm_iounmap, (void *)address))
        return NULL;
    return address;
}

void __iomem *devm_platform_get_and_ioremap_resource(struct platform_device *pdev, unsigned int index, struct resource **res)
{
    struct resource *r = platform_get_resource(pdev, IORESOURCE_MEM, index);
    void __iomem *address;

    if (res)
        *res = r;
    if (!r)
        return ERR_PTR(-EINVAL);
    address = devm_ioremap(&pdev->dev, r->start, resource_size(r));
    return address ? address : ERR_PTR(-ENOMEM);
}

void __iomem *devm_platform_ioremap_resource(struct platform_device *pdev, unsigned int index)
{
    return devm_platform_get_and_ioremap_resource(pdev, index, NULL);
}

int lc_irq_register_line(uint32_t vector, uint8_t irql, bool level_sensitive)
{
    unsigned int i;

    for (i = 0; i < LC_IRQ_MAX; ++i)
    {
        struct lc_irq_line *line = &lc_irq_lines[i];

        if (!line->present)
        {
            memset(line, 0, sizeof(*line));
            spin_lock_init(&line->lock);
            line->vector = vector;
            line->irql = irql;
            line->level_sensitive = level_sensitive;
            line->present = true;
            return (int)i + 1;
        }
    }
    return -ENOSPC;
}

static struct lc_irq_line *lc_irq_line(unsigned int irq)
{
    if (!irq || irq > LC_IRQ_MAX || !lc_irq_lines[irq - 1].present)
        return NULL;
    return &lc_irq_lines[irq - 1];
}

static void lc_irq_unmask_if_enabled(struct lc_irq_line *line)
{
    bool unmask = false;

    spin_lock(&line->lock);
    if (!line->disable_depth)
        unmask = __atomic_exchange_n(&line->masked, 0, __ATOMIC_ACQ_REL) != 0;
    spin_unlock(&line->lock);
    if (unmask)
        lc_nt_irq_unmask(line->vector, line->irql, line->level_sensitive);
}

static void lc_irq_thread(struct work_struct *work)
{
    struct lc_irq_line *line = container_of(work, struct lc_irq_line, thread_work);
    unsigned int irq = (unsigned int)(line - lc_irq_lines) + 1;

    if (line->thread_fn)
        line->thread_fn((int)irq, line->dev_id);
    lc_irq_unmask_if_enabled(line);
}

static irqreturn_t lc_irq_default_primary(int irq, void *data)
{
    (void)irq;
    (void)data;
    return IRQ_WAKE_THREAD;
}

int request_threaded_irq(unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn,
                         unsigned long flags, const char *name, void *dev_id)
{
    struct lc_irq_line *line = lc_irq_line(irq);

    if (!line)
        return -EINVAL;
    if (line->requested)
        return -EBUSY;
    if (!handler && !thread_fn)
        return -EINVAL;
    if (thread_fn)
    {
        line->thread_wq = alloc_ordered_workqueue("irq/%s", WQ_HIGHPRI, name);
        if (!line->thread_wq)
            return -ENOMEM;
        INIT_WORK(&line->thread_work, lc_irq_thread);
    }
    line->handler = handler ? handler : lc_irq_default_primary;
    line->thread_fn = thread_fn;
    line->dev_id = dev_id;
    line->flags = flags;
    line->name = name;
    line->disable_depth = 0;
    line->masked = 0;
    smp_wmb();
    WRITE_ONCE(line->requested, true);
    return 0;
}

static void lc_devm_free_irq(void *data)
{
    unsigned int irq = (unsigned int)(uintptr_t)data;

    free_irq(irq, lc_irq_lines[irq - 1].dev_id);
}

int devm_request_threaded_irq(struct device *dev, unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn,
                              unsigned long flags, const char *name, void *dev_id)
{
    int err = request_threaded_irq(irq, handler, thread_fn, flags, name, dev_id);

    if (!err)
        err = devm_add_action_or_reset(dev, lc_devm_free_irq, (void *)(uintptr_t)irq);
    return err;
}

const void *free_irq(unsigned int irq, void *dev_id)
{
    struct lc_irq_line *line = lc_irq_line(irq);

    if (!line || !line->requested || line->dev_id != dev_id)
        return NULL;
    disable_irq(irq);
    WRITE_ONCE(line->requested, false);
    if (line->thread_wq)
    {
        destroy_workqueue(line->thread_wq);
        line->thread_wq = NULL;
    }
    line->handler = NULL;
    line->thread_fn = NULL;
    return line->name;
}

void disable_irq_nosync(unsigned int irq)
{
    struct lc_irq_line *line = lc_irq_line(irq);
    bool mask;

    if (!line)
        return;
    spin_lock(&line->lock);
    mask = line->disable_depth++ == 0 && __atomic_exchange_n(&line->masked, 1, __ATOMIC_ACQ_REL) == 0;
    spin_unlock(&line->lock);
    if (mask)
        lc_nt_irq_mask(line->vector, line->irql);
}

void synchronize_irq(unsigned int irq)
{
    struct lc_irq_line *line = lc_irq_line(irq);

    if (line && line->thread_wq)
        flush_work(&line->thread_work);
}

void disable_irq(unsigned int irq)
{
    disable_irq_nosync(irq);
    synchronize_irq(irq);
}

void enable_irq(unsigned int irq)
{
    struct lc_irq_line *line = lc_irq_line(irq);
    bool pending_thread;

    if (!line)
        return;
    spin_lock(&line->lock);
    if (WARN_ON(!line->disable_depth))
    {
        spin_unlock(&line->lock);
        return;
    }
    line->disable_depth--;
    spin_unlock(&line->lock);
    pending_thread = line->thread_wq && (work_pending(&line->thread_work));
    if (!pending_thread)
        lc_irq_unmask_if_enabled(line);
}

bool lc_irq_service(unsigned int irq, bool *queue_dpc)
{
    struct lc_irq_line *line = lc_irq_line(irq);
    irqreturn_t ret;

    *queue_dpc = false;
    if (!line || !READ_ONCE(line->requested))
        return false;

    ret = line->handler((int)irq, line->dev_id);
    if (ret == IRQ_WAKE_THREAD)
    {
        if (!line->thread_wq)
            return true;
        __atomic_store_n(&line->masked, 1, __ATOMIC_RELEASE);
        lc_nt_irq_mask(line->vector, line->irql);
        *queue_dpc = true;
        return true;
    }
    return ret != IRQ_NONE;
}

void lc_irq_dpc(unsigned int irq)
{
    struct lc_irq_line *line = lc_irq_line(irq);

    if (!line || !line->thread_wq)
        return;
    queue_work(line->thread_wq, &line->thread_work);
}

bool lc_in_hardirq(void)
{
    return !lc_nt_at_passive();
}

struct clk *lc_device_add_clk(struct device *dev, const char *id, const struct lc_clk_ops *ops, void *context)
{
    struct clk *clk = kzalloc(sizeof(*clk), GFP_KERNEL);

    if (!clk)
        return NULL;
    clk->id = id;
    clk->ops = ops;
    clk->context = context;
    list_add_tail(&clk->node, &dev->lc_clks);
    return clk;
}

static struct clk *lc_find_clk(struct device *dev, const char *id)
{
    struct clk *clk;

    list_for_each_entry(clk, &dev->lc_clks, node)
    {
        if ((!id && !clk->id) || (id && clk->id && !strcmp(id, clk->id)))
            return clk;
    }
    return NULL;
}

struct clk *devm_clk_get(struct device *dev, const char *id)
{
    struct clk *clk = lc_find_clk(dev, id);

    return clk ? clk : ERR_PTR(-ENOENT);
}

struct clk *devm_clk_get_optional(struct device *dev, const char *id)
{
    return lc_find_clk(dev, id);
}

int clk_prepare_enable(struct clk *clk)
{
    int err = 0;

    if (!clk)
        return 0;
    if (clk->enable_count++ == 0 && clk->ops && clk->ops->enable)
    {
        err = clk->ops->enable(clk->context);
        if (err)
            clk->enable_count--;
    }
    return err;
}

void clk_disable_unprepare(struct clk *clk)
{
    if (!clk || WARN_ON(!clk->enable_count))
        return;
    if (--clk->enable_count == 0 && clk->ops && clk->ops->disable)
        clk->ops->disable(clk->context);
}

unsigned long clk_get_rate(struct clk *clk)
{
    if (!clk || !clk->ops || !clk->ops->get_rate)
        return 0;
    return clk->ops->get_rate(clk->context);
}

static int lc_pm_callback(struct device *dev, bool resume)
{
    const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
    int (*callback)(struct device *) = NULL;

    if (pm)
        callback = resume ? pm->runtime_resume : pm->runtime_suspend;
    return callback ? callback(dev) : 0;
}

static int lc_pm_resume_locked(struct device *dev)
{
    bool active, enabled;
    int err;

    spin_lock(&dev->lc_pm.state_lock);
    active = dev->lc_pm.active;
    enabled = dev->lc_pm.enabled;
    spin_unlock(&dev->lc_pm.state_lock);
    if (active)
        return 1;
    if (!enabled)
        return -EACCES;
    err = lc_pm_callback(dev, true);
    spin_lock(&dev->lc_pm.state_lock);
    if (err)
        dev->lc_pm.error = true;
    else
        dev->lc_pm.active = true;
    spin_unlock(&dev->lc_pm.state_lock);
    return err;
}

static int lc_pm_suspend_locked(struct device *dev)
{
    bool idle;
    int err;

    spin_lock(&dev->lc_pm.state_lock);
    idle = dev->lc_pm.active && dev->lc_pm.usage <= 0;
    spin_unlock(&dev->lc_pm.state_lock);
    if (!idle)
        return 0;
    err = lc_pm_callback(dev, false);
    spin_lock(&dev->lc_pm.state_lock);
    if (err)
        dev->lc_pm.error = true;
    else
        dev->lc_pm.active = false;
    spin_unlock(&dev->lc_pm.state_lock);
    return err;
}

static void lc_pm_usage_add(struct device *dev, int delta)
{
    spin_lock(&dev->lc_pm.state_lock);
    dev->lc_pm.usage += delta;
    spin_unlock(&dev->lc_pm.state_lock);
}

int pm_runtime_get_sync(struct device *dev)
{
    int err;

    lc_pm_usage_add(dev, 1);
    mutex_lock(&dev->lc_pm.lock);
    err = lc_pm_resume_locked(dev);
    mutex_unlock(&dev->lc_pm.lock);
    return err;
}

int pm_runtime_resume_and_get(struct device *dev)
{
    int err;

    lc_pm_usage_add(dev, 1);
    mutex_lock(&dev->lc_pm.lock);
    err = lc_pm_resume_locked(dev);
    mutex_unlock(&dev->lc_pm.lock);
    if (err < 0)
        lc_pm_usage_add(dev, -1);
    return err < 0 ? err : 0;
}

static int lc_pm_get_if(struct device *dev, bool in_use)
{
    int ret;

    spin_lock(&dev->lc_pm.state_lock);
    if (!dev->lc_pm.enabled)
        ret = -EINVAL;
    else if (dev->lc_pm.active && (!in_use || dev->lc_pm.usage > 0))
    {
        dev->lc_pm.usage++;
        ret = 1;
    }
    else
        ret = 0;
    spin_unlock(&dev->lc_pm.state_lock);
    return ret;
}

int pm_runtime_get_if_in_use(struct device *dev)
{
    return lc_pm_get_if(dev, true);
}

int pm_runtime_get_if_active(struct device *dev)
{
    return lc_pm_get_if(dev, false);
}

static int lc_pm_put(struct device *dev)
{
    int err = 0;

    spin_lock(&dev->lc_pm.state_lock);
    if (WARN_ON(dev->lc_pm.usage <= 0))
        err = -EINVAL;
    else
        dev->lc_pm.usage--;
    spin_unlock(&dev->lc_pm.state_lock);
    return err;
}

int pm_runtime_put(struct device *dev)
{
    return lc_pm_put(dev);
}

int pm_runtime_put_autosuspend(struct device *dev)
{
    return lc_pm_put(dev);
}

void pm_runtime_put_noidle(struct device *dev)
{
    lc_pm_put(dev);
}

int pm_runtime_put_sync(struct device *dev)
{
    return lc_pm_put(dev);
}

int pm_runtime_put_sync_suspend(struct device *dev)
{
    bool enabled;
    int err;

    err = lc_pm_put(dev);
    if (err)
        return err;
    mutex_lock(&dev->lc_pm.lock);
    spin_lock(&dev->lc_pm.state_lock);
    enabled = dev->lc_pm.enabled;
    spin_unlock(&dev->lc_pm.state_lock);
    if (enabled)
        err = lc_pm_suspend_locked(dev);
    mutex_unlock(&dev->lc_pm.lock);
    return err;
}

int pm_runtime_suspend(struct device *dev)
{
    bool enabled;
    int err;

    mutex_lock(&dev->lc_pm.lock);
    spin_lock(&dev->lc_pm.state_lock);
    enabled = dev->lc_pm.enabled;
    spin_unlock(&dev->lc_pm.state_lock);
    err = enabled ? lc_pm_suspend_locked(dev) : -EACCES;
    mutex_unlock(&dev->lc_pm.lock);
    return err;
}

int pm_runtime_resume(struct device *dev)
{
    int err;

    mutex_lock(&dev->lc_pm.lock);
    err = lc_pm_resume_locked(dev);
    mutex_unlock(&dev->lc_pm.lock);
    return err < 0 ? err : 0;
}

static void lc_pm_set_state(struct device *dev, bool active)
{
    spin_lock(&dev->lc_pm.state_lock);
    dev->lc_pm.active = active;
    dev->lc_pm.error = false;
    spin_unlock(&dev->lc_pm.state_lock);
}

int pm_runtime_set_active(struct device *dev)
{
    lc_pm_set_state(dev, true);
    return 0;
}

int pm_runtime_set_suspended(struct device *dev)
{
    lc_pm_set_state(dev, false);
    return 0;
}

static void lc_pm_set_enabled(struct device *dev, bool enabled)
{
    spin_lock(&dev->lc_pm.state_lock);
    dev->lc_pm.enabled = enabled;
    spin_unlock(&dev->lc_pm.state_lock);
}

void pm_runtime_enable(struct device *dev)
{
    lc_pm_set_enabled(dev, true);
}

void pm_runtime_disable(struct device *dev)
{
    lc_pm_set_enabled(dev, false);
}

static void lc_devm_pm_runtime_disable(void *data)
{
    pm_runtime_disable(data);
}

int devm_pm_runtime_enable(struct device *dev)
{
    pm_runtime_enable(dev);
    return devm_add_action_or_reset(dev, lc_devm_pm_runtime_disable, dev);
}

bool pm_runtime_status_suspended(struct device *dev)
{
    return !READ_ONCE(dev->lc_pm.active);
}

bool pm_runtime_active(struct device *dev)
{
    return READ_ONCE(dev->lc_pm.active);
}

const struct lc_of_property *lc_of_find_property(const struct device_node *np, const char *name)
{
    unsigned int i;

    if (!np)
        return NULL;
    for (i = 0; i < np->property_count; ++i)
    {
        if (!strcmp(np->properties[i].name, name))
            return &np->properties[i];
    }
    return NULL;
}

bool of_device_is_compatible(const struct device_node *np, const char *compatible)
{
    unsigned int i;

    if (!np)
        return false;
    for (i = 0; i < np->compatible_count; ++i)
    {
        if (!strcmp(np->compatible[i], compatible))
            return true;
    }
    return false;
}

const void *of_device_get_match_data(const struct device *dev)
{
    const struct of_device_id *match;
    unsigned int i;

    if (!dev->driver || !dev->driver->of_match_table || !dev->of_node)
        return NULL;
    for (i = 0; i < dev->of_node->compatible_count; ++i)
    {
        for (match = dev->driver->of_match_table; match->compatible[0]; ++match)
        {
            if (!strcmp(match->compatible, dev->of_node->compatible[i]))
                return match->data;
        }
    }
    return NULL;
}

int request_firmware(const struct firmware **fw, const char *name, struct device *dev)
{
    struct firmware *image;
    void *data;
    size_t size;
    int err;

    *fw = NULL;
    image = kzalloc(sizeof(*image), GFP_KERNEL);
    if (!image)
        return -ENOMEM;
    err = lc_nt_read_file(name, &data, &size);
    if (err)
    {
        dev_err(dev, "firmware %s not found (%d)\n", name, err);
        kfree(image);
        return err;
    }
    image->data = data;
    image->size = size;
    image->priv = data;
    *fw = image;
    return 0;
}

int firmware_request_nowarn(const struct firmware **fw, const char *name, struct device *dev)
{
    struct firmware *image;
    void *data;
    size_t size;
    int err;

    (void)dev;
    *fw = NULL;
    image = kzalloc(sizeof(*image), GFP_KERNEL);
    if (!image)
        return -ENOMEM;
    err = lc_nt_read_file(name, &data, &size);
    if (err)
    {
        kfree(image);
        return err;
    }
    image->data = data;
    image->size = size;
    image->priv = data;
    *fw = image;
    return 0;
}

void release_firmware(const struct firmware *fw)
{
    if (!fw)
        return;
    lc_nt_free_file(fw->priv);
    kfree(fw);
}
