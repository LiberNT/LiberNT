/*
 * PROJECT:     LiberNT Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux workqueues, delayed work, timers and task identity
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <linux_compat.h>

#define LC_WQ_MAX_WORKERS 4
#define LC_WQ_INLINE_SLOTS 4
#define LC_WORK_BATCH_SLOTS 8
#define LC_WORK_BATCH_QUEUES 4

enum
{
    LC_DWORK_IDLE,
    LC_DWORK_ARMED,
    LC_DWORK_CANCELLED,
};

struct lc_worker
{
    struct workqueue_struct *wq;
    void *thread;
    void *thread_id;
    struct work_struct *running;
};

struct workqueue_struct
{
    struct list_head node;
    char name[32];
    unsigned int flags;
    struct list_head pending;
    lc_nt_event wake;
    wait_queue_head_t idle;
    bool stopping;
    unsigned int nr_workers;
    struct lc_worker workers[LC_WQ_MAX_WORKERS];
    struct lc_worker inline_slots[LC_WQ_INLINE_SLOTS];
};

struct lc_work_batch
{
    void *thread_id;
    unsigned int depth;
    unsigned int count;
    struct workqueue_struct *queues[LC_WORK_BATCH_QUEUES];
};

static struct lc_work_batch lc_work_batches[LC_WORK_BATCH_SLOTS];
static atomic_t lc_work_batch_active;

static DEFINE_SPINLOCK(lc_work_lock);
static LIST_HEAD(lc_workqueues);
static struct workqueue_struct *lc_default_wq;
static struct task_struct lc_task;
static DEFINE_SPINLOCK(lc_task_lock);
static LIST_HEAD(lc_tasks);

static bool lc_work_running_locked(struct work_struct *work)
{
    struct workqueue_struct *wq = work->wq;
    unsigned int i;

    if (!wq)
        return false;
    for (i = 0; i < wq->nr_workers; ++i)
    {
        if (wq->workers[i].running == work)
            return true;
    }
    for (i = 0; i < LC_WQ_INLINE_SLOTS; ++i)
    {
        if (wq->inline_slots[i].running == work)
            return true;
    }
    return false;
}

struct work_struct *lc_current_work(void);

static bool lc_work_running(struct work_struct *work)
{
    bool running;

    spin_lock(&lc_work_lock);
    running = lc_work_running_locked(work);
    spin_unlock(&lc_work_lock);
    return running;
}

static void lc_worker_main(void *context)
{
    struct lc_worker *worker = context;
    struct workqueue_struct *wq = worker->wq;

    worker->thread_id = lc_nt_current_thread();
    for (;;)
    {
        struct work_struct *work = NULL, *candidate;
        bool more = false;

        spin_lock(&lc_work_lock);
        list_for_each_entry(candidate, &wq->pending, entry)
        {
            if (!lc_work_running_locked(candidate))
            {
                work = candidate;
                break;
            }
        }
        if (work)
        {
            list_del_init(&work->entry);
            clear_bit(WORK_STRUCT_PENDING_BIT, &work->state);
            worker->running = work;
            more = !list_empty(&wq->pending);
        }
        else if (wq->stopping)
        {
            spin_unlock(&lc_work_lock);
            break;
        }
        spin_unlock(&lc_work_lock);

        if (!work)
        {
            lc_nt_event_wait(&wq->wake, -1);
            continue;
        }
        if (more)
            lc_nt_event_set(&wq->wake);

        work->func(work);

        spin_lock(&lc_work_lock);
        worker->running = NULL;
        spin_unlock(&lc_work_lock);
        lc_wake_up_all(&wq->idle);
    }
    lc_nt_event_set(&wq->wake);
}

void lc_init_work(struct work_struct *work, work_func_t func)
{
    INIT_LIST_HEAD(&work->entry);
    work->func = func;
    work->state = 0;
    work->wq = NULL;
}

static void lc_delayed_work_timer(void *context);

void lc_init_delayed_work(struct delayed_work *dwork, work_func_t func)
{
    lc_init_work(&dwork->work, func);
    lc_nt_timer_init(&dwork->timer.nt, lc_delayed_work_timer, dwork);
    dwork->wq = NULL;
    dwork->timer_armed = LC_DWORK_IDLE;
}

struct workqueue_struct *lc_alloc_workqueue(const char *name, unsigned int flags, int max_active)
{
    struct workqueue_struct *wq;
    unsigned int i, count;

    wq = kzalloc(sizeof(*wq), GFP_KERNEL);
    if (!wq)
        return NULL;
    strscpy_impl(wq->name, name ? name : "wq", sizeof(wq->name));
    wq->flags = flags;
    INIT_LIST_HEAD(&wq->pending);
    lc_nt_event_init(&wq->wake, 1, 0);
    init_waitqueue_head(&wq->idle);

    if (flags & __WQ_ORDERED)
        count = 1;
    else
    {
        count = max_active > 0 ? (unsigned int)max_active : LC_WQ_MAX_WORKERS;
        count = min(count, (unsigned int)LC_WQ_MAX_WORKERS);
        count = min(count, max(lc_nt_processor_count(), 1U));
    }

    for (i = 0; i < count; ++i)
    {
        wq->workers[i].wq = wq;
        if (lc_nt_create_thread(lc_worker_main, &wq->workers[i], &wq->workers[i].thread))
            break;
        wq->nr_workers++;
    }
    if (!wq->nr_workers)
    {
        kfree(wq);
        return NULL;
    }

    spin_lock(&lc_work_lock);
    list_add(&wq->node, &lc_workqueues);
    spin_unlock(&lc_work_lock);
    return wq;
}

void lc_destroy_workqueue(struct workqueue_struct *wq)
{
    unsigned int i;

    if (!wq)
        return;
    lc_flush_workqueue(wq);
    spin_lock(&lc_work_lock);
    wq->stopping = true;
    list_del(&wq->node);
    spin_unlock(&lc_work_lock);
    lc_nt_event_set(&wq->wake);
    for (i = 0; i < wq->nr_workers; ++i)
        lc_nt_wait_thread(wq->workers[i].thread);
    kfree(wq);
}

struct workqueue_struct *lc_system_wq(void)
{
    WARN_ON(!lc_default_wq);
    return lc_default_wq;
}

static void lc_enqueue_locked(struct workqueue_struct *wq, struct work_struct *work)
{
    work->wq = wq;
    list_add_tail(&work->entry, &wq->pending);
}

static bool lc_thread_runs_wq(struct workqueue_struct *wq)
{
    void *thread = lc_nt_current_thread();
    unsigned int i;

    for (i = 0; i < wq->nr_workers; ++i)
    {
        if (READ_ONCE(wq->workers[i].thread_id) == thread)
            return true;
    }
    for (i = 0; i < LC_WQ_INLINE_SLOTS; ++i)
    {
        if (READ_ONCE(wq->inline_slots[i].thread_id) == thread)
            return true;
    }
    return false;
}

static bool lc_work_batch_note(struct workqueue_struct *wq)
{
    void *thread;
    unsigned int i, j;

    if (!atomic_read(&lc_work_batch_active))
        return false;
    thread = lc_nt_current_thread();
    for (i = 0; i < LC_WORK_BATCH_SLOTS; ++i)
    {
        struct lc_work_batch *batch = &lc_work_batches[i];

        if (READ_ONCE(batch->thread_id) != thread)
            continue;
        for (j = 0; j < batch->count; ++j)
        {
            if (batch->queues[j] == wq)
                return true;
        }
        if (batch->count == LC_WORK_BATCH_QUEUES)
            return false;
        batch->queues[batch->count++] = wq;
        return true;
    }
    return false;
}

static void lc_work_run_pending(struct workqueue_struct *wq)
{
    struct workqueue_struct *iter;
    struct lc_worker *slot = NULL;
    void *thread = lc_nt_current_thread();
    bool passive = lc_nt_at_passive();
    bool live = false;
    unsigned int i;

    spin_lock(&lc_work_lock);
    list_for_each_entry(iter, &lc_workqueues, node)
    {
        if (iter == wq)
        {
            live = true;
            break;
        }
    }
    if (!live || wq->stopping || list_empty(&wq->pending))
    {
        spin_unlock(&lc_work_lock);
        return;
    }
    for (i = 0; passive && i < LC_WQ_INLINE_SLOTS; ++i)
    {
        if (!wq->inline_slots[i].thread_id)
        {
            slot = &wq->inline_slots[i];
            break;
        }
    }
    if (!slot)
    {
        spin_unlock(&lc_work_lock);
        lc_nt_event_set(&wq->wake);
        return;
    }
    slot->wq = wq;
    slot->thread_id = thread;
    for (;;)
    {
        struct work_struct *work = NULL, *candidate;

        list_for_each_entry(candidate, &wq->pending, entry)
        {
            if (!lc_work_running_locked(candidate))
            {
                work = candidate;
                break;
            }
        }
        if (!work)
            break;
        list_del_init(&work->entry);
        clear_bit(WORK_STRUCT_PENDING_BIT, &work->state);
        slot->running = work;
        spin_unlock(&lc_work_lock);

        work->func(work);

        spin_lock(&lc_work_lock);
        slot->running = NULL;
        spin_unlock(&lc_work_lock);
        lc_wake_up_all(&wq->idle);
        spin_lock(&lc_work_lock);
    }
    slot->thread_id = NULL;
    spin_unlock(&lc_work_lock);
}

void lc_work_batch_unplug(void)
{
    void *thread;
    unsigned int i, j;

    if (!atomic_read(&lc_work_batch_active))
        return;
    thread = lc_nt_current_thread();
    for (i = 0; i < LC_WORK_BATCH_SLOTS; ++i)
    {
        struct lc_work_batch *batch = &lc_work_batches[i];

        if (READ_ONCE(batch->thread_id) != thread)
            continue;
        for (j = 0; j < batch->count; ++j)
            lc_nt_event_set(&batch->queues[j]->wake);
        batch->count = 0;
        return;
    }
}

void lc_work_batch_begin(void)
{
    void *thread = lc_nt_current_thread();
    struct lc_work_batch *free_slot = NULL;
    unsigned int i;

    spin_lock(&lc_work_lock);
    for (i = 0; i < LC_WORK_BATCH_SLOTS; ++i)
    {
        struct lc_work_batch *batch = &lc_work_batches[i];

        if (batch->thread_id == thread)
        {
            batch->depth++;
            spin_unlock(&lc_work_lock);
            return;
        }
        if (!batch->thread_id && !free_slot)
            free_slot = batch;
    }
    if (free_slot)
    {
        free_slot->count = 0;
        free_slot->depth = 1;
        WRITE_ONCE(free_slot->thread_id, thread);
        atomic_inc(&lc_work_batch_active);
    }
    spin_unlock(&lc_work_lock);
}

void lc_work_batch_end(void)
{
    struct workqueue_struct *queues[LC_WORK_BATCH_QUEUES];
    void *thread = lc_nt_current_thread();
    unsigned int i, count = 0;

    spin_lock(&lc_work_lock);
    for (i = 0; i < LC_WORK_BATCH_SLOTS; ++i)
    {
        struct lc_work_batch *batch = &lc_work_batches[i];

        if (batch->thread_id != thread)
            continue;
        if (--batch->depth)
            break;
        count = batch->count;
        memcpy(queues, batch->queues, sizeof(queues));
        batch->count = 0;
        WRITE_ONCE(batch->thread_id, NULL);
        atomic_dec(&lc_work_batch_active);
        break;
    }
    spin_unlock(&lc_work_lock);

    for (i = 0; i < count; ++i)
        lc_work_run_pending(queues[i]);
}

bool lc_queue_work_tail(struct workqueue_struct *wq, struct work_struct *work)
{
    if (test_and_set_bit(WORK_STRUCT_PENDING_BIT, &work->state))
        return false;
    spin_lock(&lc_work_lock);
    lc_enqueue_locked(wq, work);
    spin_unlock(&lc_work_lock);
    if (!lc_thread_runs_wq(wq) && !lc_work_batch_note(wq))
        lc_nt_event_set(&wq->wake);
    return true;
}

bool lc_queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
    if (test_and_set_bit(WORK_STRUCT_PENDING_BIT, &work->state))
        return false;
    spin_lock(&lc_work_lock);
    lc_enqueue_locked(wq, work);
    spin_unlock(&lc_work_lock);
    if (!lc_work_batch_note(wq))
        lc_nt_event_set(&wq->wake);
    return true;
}

static void lc_delayed_work_timer(void *context)
{
    struct delayed_work *dwork = context;
    struct workqueue_struct *wq;

    spin_lock(&lc_work_lock);
    wq = dwork->wq;
    if (dwork->timer_armed == LC_DWORK_CANCELLED)
    {
        dwork->timer_armed = LC_DWORK_IDLE;
        clear_bit(WORK_STRUCT_PENDING_BIT, &dwork->work.state);
        spin_unlock(&lc_work_lock);
        return;
    }
    dwork->timer_armed = LC_DWORK_IDLE;
    lc_enqueue_locked(wq, &dwork->work);
    spin_unlock(&lc_work_lock);
    lc_nt_event_set(&wq->wake);
}

static void lc_arm_delayed_locked(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay)
{
    dwork->wq = wq;
    dwork->work.wq = wq;
    if (!delay)
    {
        lc_enqueue_locked(wq, &dwork->work);
        return;
    }
    dwork->timer_armed = LC_DWORK_ARMED;
    dwork->timer.expires = jiffies + delay;
    lc_nt_timer_set(&dwork->timer.nt, (int64_t)delay * 10000);
}

bool lc_queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay)
{
    if (test_and_set_bit(WORK_STRUCT_PENDING_BIT, &dwork->work.state))
        return false;
    spin_lock(&lc_work_lock);
    lc_arm_delayed_locked(wq, dwork, delay);
    spin_unlock(&lc_work_lock);
    if (!delay)
        lc_nt_event_set(&wq->wake);
    return true;
}

static bool lc_try_cancel_pending_locked(struct delayed_work *dwork, struct work_struct *work)
{
    if (dwork && dwork->timer_armed == LC_DWORK_ARMED)
    {
        if (lc_nt_timer_cancel(&dwork->timer.nt))
        {
            dwork->timer_armed = LC_DWORK_IDLE;
            clear_bit(WORK_STRUCT_PENDING_BIT, &work->state);
        }
        else
        {
            dwork->timer_armed = LC_DWORK_CANCELLED;
        }
        return true;
    }
    if (test_bit(WORK_STRUCT_PENDING_BIT, &work->state) && !list_empty(&work->entry))
    {
        list_del_init(&work->entry);
        clear_bit(WORK_STRUCT_PENDING_BIT, &work->state);
        return true;
    }
    return false;
}

bool lc_mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay)
{
    bool was_pending;

    spin_lock(&lc_work_lock);
    was_pending = lc_try_cancel_pending_locked(dwork, &dwork->work);
    if (dwork->timer_armed == LC_DWORK_CANCELLED)
    {
        spin_unlock(&lc_work_lock);
        while (READ_ONCE(dwork->timer_armed) == LC_DWORK_CANCELLED)
            lc_nt_yield();
        spin_lock(&lc_work_lock);
    }
    set_bit(WORK_STRUCT_PENDING_BIT, &dwork->work.state);
    lc_arm_delayed_locked(wq, dwork, delay);
    spin_unlock(&lc_work_lock);
    if (!delay)
        lc_nt_event_set(&wq->wake);
    return was_pending;
}

bool lc_cancel_work(struct work_struct *work)
{
    bool cancelled;

    spin_lock(&lc_work_lock);
    cancelled = lc_try_cancel_pending_locked(NULL, work);
    spin_unlock(&lc_work_lock);
    return cancelled;
}

static void lc_wait_work_idle(struct work_struct *work, bool wait_pending)
{
    struct workqueue_struct *wq = READ_ONCE(work->wq);

    if (!wq)
        return;
    if (lc_current_work() == work)
        return;
    if (wait_pending && test_bit(WORK_STRUCT_PENDING_BIT, &work->state))
        lc_nt_event_set(&wq->wake);
    wait_event(wq->idle, !lc_work_running(work) &&
               (!wait_pending || !test_bit(WORK_STRUCT_PENDING_BIT, &work->state)));
}

bool lc_cancel_work_sync(struct work_struct *work)
{
    bool cancelled = lc_cancel_work(work);

    lc_wait_work_idle(work, false);
    return cancelled;
}

bool lc_cancel_delayed_work(struct delayed_work *dwork)
{
    bool cancelled;

    spin_lock(&lc_work_lock);
    cancelled = lc_try_cancel_pending_locked(dwork, &dwork->work);
    spin_unlock(&lc_work_lock);
    return cancelled;
}

bool lc_cancel_delayed_work_sync(struct delayed_work *dwork)
{
    bool cancelled = lc_cancel_delayed_work(dwork);

    while (READ_ONCE(dwork->timer_armed) == LC_DWORK_CANCELLED)
        lc_nt_yield();
    lc_wait_work_idle(&dwork->work, false);
    return cancelled;
}

bool lc_flush_work(struct work_struct *work)
{
    bool busy = test_bit(WORK_STRUCT_PENDING_BIT, &work->state) || lc_work_running(work);

    lc_wait_work_idle(work, true);
    return busy;
}

bool lc_flush_delayed_work(struct delayed_work *dwork)
{
    bool queued = false;

    spin_lock(&lc_work_lock);
    if (dwork->timer_armed == LC_DWORK_ARMED && lc_nt_timer_cancel(&dwork->timer.nt))
    {
        dwork->timer_armed = LC_DWORK_IDLE;
        lc_enqueue_locked(dwork->wq, &dwork->work);
        queued = true;
    }
    spin_unlock(&lc_work_lock);
    if (queued)
        lc_nt_event_set(&dwork->wq->wake);
    return lc_flush_work(&dwork->work);
}

static bool lc_workqueue_idle(struct workqueue_struct *wq)
{
    bool idle;
    unsigned int i;

    spin_lock(&lc_work_lock);
    idle = list_empty(&wq->pending);
    for (i = 0; idle && i < wq->nr_workers; ++i)
        idle = wq->workers[i].running == NULL;
    for (i = 0; idle && i < LC_WQ_INLINE_SLOTS; ++i)
        idle = wq->inline_slots[i].running == NULL;
    spin_unlock(&lc_work_lock);
    return idle;
}

void lc_flush_workqueue(struct workqueue_struct *wq)
{
    lc_nt_event_set(&wq->wake);
    wait_event(wq->idle, lc_workqueue_idle(wq));
}

bool lc_work_pending(struct work_struct *work)
{
    return test_bit(WORK_STRUCT_PENDING_BIT, &work->state);
}

struct work_struct *lc_current_work(void)
{
    struct workqueue_struct *wq;
    struct work_struct *work = NULL;
    void *thread = lc_nt_current_thread();
    unsigned int i;

    spin_lock(&lc_work_lock);
    list_for_each_entry(wq, &lc_workqueues, node)
    {
        for (i = 0; i < wq->nr_workers; ++i)
        {
            if (wq->workers[i].thread_id == thread)
            {
                work = wq->workers[i].running;
                goto out;
            }
        }
        for (i = 0; i < LC_WQ_INLINE_SLOTS; ++i)
        {
            if (wq->inline_slots[i].thread_id == thread)
            {
                work = wq->inline_slots[i].running;
                goto out;
            }
        }
    }
out:
    spin_unlock(&lc_work_lock);
    return work;
}

static void lc_timer_list_fire(void *context)
{
    struct timer_list *timer = context;

    timer->function(timer);
}

void lc_timer_setup(struct timer_list *timer, void (*function)(struct timer_list *), unsigned int flags)
{
    (void)flags;
    timer->function = function;
    timer->expires = 0;
    lc_nt_timer_init(&timer->nt, lc_timer_list_fire, timer);
    timer->initialized = 1;
}

int lc_mod_timer(struct timer_list *timer, unsigned long expires)
{
    long delta = (long)(expires - jiffies);
    int was_pending = lc_nt_timer_cancel(&timer->nt);

    timer->expires = expires;
    lc_nt_timer_set(&timer->nt, delta > 0 ? (int64_t)delta * 10000 : 1);
    return was_pending;
}

int lc_del_timer(struct timer_list *timer)
{
    return lc_nt_timer_cancel(&timer->nt);
}

static void lc_task_thread_exit(void *thread_id)
{
    struct task_struct *task, *found = NULL;

    spin_lock(&lc_task_lock);
    list_for_each_entry(task, &lc_tasks, lc_node)
    {
        if (task->lc_thread_id == thread_id)
        {
            list_del(&task->lc_node);
            found = task;
            break;
        }
    }
    spin_unlock(&lc_task_lock);
    kfree(found);
}

int lc_compat_init(void)
{
    int err;

    err = lc_nt_set_thread_exit_callback(lc_task_thread_exit);
    if (err)
        return err;
    lc_default_wq = lc_alloc_workqueue("events", 0, LC_WQ_MAX_WORKERS);
    if (!lc_default_wq)
    {
        lc_nt_clear_thread_exit_callback();
        return -ENOMEM;
    }
    return 0;
}

void lc_compat_exit(void)
{
    struct task_struct *task, *next;

    lc_destroy_workqueue(lc_default_wq);
    lc_default_wq = NULL;
    lc_nt_clear_thread_exit_callback();
    spin_lock(&lc_task_lock);
    list_for_each_entry_safe(task, next, &lc_tasks, lc_node)
    {
        list_del(&task->lc_node);
        kfree(task);
    }
    spin_unlock(&lc_task_lock);
}

struct task_struct *lc_current_task(void)
{
    void *thread_id = lc_nt_current_thread_id();
    struct task_struct *task;

    spin_lock(&lc_task_lock);
    list_for_each_entry(task, &lc_tasks, lc_node)
    {
        if (task->lc_thread_id == thread_id)
            goto found;
    }
    task = kzalloc(sizeof(*task), GFP_ATOMIC);
    if (!task)
    {
        spin_unlock(&lc_task_lock);
        return &lc_task;
    }
    task->lc_thread_id = thread_id;
    lc_nt_event_init(&task->lc_wake, 1, 0);
    list_add(&task->lc_node, &lc_tasks);
found:
    spin_unlock(&lc_task_lock);
    task->group_leader = &lc_task;
    task->tgid = (pid_t)lc_nt_current_pid();
    task->pid = task->tgid;
    lc_task.group_leader = &lc_task;
    lc_task.tgid = task->tgid;
    return task;
}

long lc_schedule_timeout(long timeout)
{
    struct task_struct *task = lc_current_task();
    u64 start;
    long elapsed;

    if (task == &lc_task)
    {
        lc_nt_sleep_100ns(10000);
        return timeout == MAX_SCHEDULE_TIMEOUT ? timeout : max(timeout - 1, 0L);
    }
    if (timeout == MAX_SCHEDULE_TIMEOUT)
    {
        lc_nt_event_wait(&task->lc_wake, -1);
        return timeout;
    }
    if (timeout <= 0)
        return 0;
    start = lc_nt_time_ns();
    if (!lc_nt_event_wait(&task->lc_wake, (int64_t)timeout * 10000))
        return 0;
    elapsed = (long)((lc_nt_time_ns() - start) / 1000000ULL);
    return elapsed >= timeout ? 1 : timeout - elapsed;
}

int lc_wake_up_process(struct task_struct *task)
{
    if (!task || task == &lc_task)
        return 0;
    lc_nt_event_set(&task->lc_wake);
    return 1;
}
