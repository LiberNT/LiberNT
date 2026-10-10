/*
 * PROJECT:     LiberNT Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux mutexes, wait queues and RCU deferral
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <linux_compat.h>

static DEFINE_SPINLOCK(lc_init_lock);

static void lc_once(int *initialized, void (*init)(void *), void *object)
{
    if (__atomic_load_n(initialized, __ATOMIC_ACQUIRE))
        return;
    spin_lock(&lc_init_lock);
    if (!*initialized)
    {
        init(object);
        __atomic_store_n(initialized, 1, __ATOMIC_RELEASE);
    }
    spin_unlock(&lc_init_lock);
}

static void lc_waitqueue_setup(void *object)
{
    wait_queue_head_t *wq = object;

    spin_lock_init(&wq->lock);
    INIT_LIST_HEAD(&wq->head);
}

void lc_init_waitqueue_head(wait_queue_head_t *wq)
{
    lc_waitqueue_setup(wq);
    __atomic_store_n(&wq->initialized, 1, __ATOMIC_RELEASE);
}

void lc_prepare_to_wait(wait_queue_head_t *wq, struct lc_waiter *waiter)
{
    lc_once(&wq->initialized, lc_waitqueue_setup, wq);
    spin_lock(&wq->lock);
    if (!waiter->queued)
    {
        lc_nt_event_init(&waiter->event, 0, 0);
        list_add_tail(&waiter->node, &wq->head);
        waiter->queued = true;
    }
    else
    {
        lc_nt_event_clear(&waiter->event);
    }
    spin_unlock(&wq->lock);
}

void lc_finish_wait(wait_queue_head_t *wq, struct lc_waiter *waiter)
{
    if (!waiter->queued)
        return;
    spin_lock(&wq->lock);
    list_del(&waiter->node);
    waiter->queued = false;
    spin_unlock(&wq->lock);
}

long lc_waiter_sleep(struct lc_waiter *waiter, long timeout)
{
    u64 start, elapsed_ms;

    lc_work_batch_unplug();
    if (timeout == MAX_SCHEDULE_TIMEOUT)
    {
        lc_nt_event_wait(&waiter->event, -1);
        return timeout;
    }

    start = lc_nt_time_ns();
    lc_nt_event_wait(&waiter->event, (int64_t)timeout * 10000);
    elapsed_ms = (lc_nt_time_ns() - start) / 1000000ULL;
    if (elapsed_ms >= (u64)timeout)
        return 0;
    return timeout - (long)elapsed_ms;
}

void lc_wake_up_all(wait_queue_head_t *wq)
{
    struct lc_waiter *waiter;

    lc_once(&wq->initialized, lc_waitqueue_setup, wq);
    spin_lock(&wq->lock);
    list_for_each_entry(waiter, &wq->head, node)
        lc_nt_event_set(&waiter->event);
    spin_unlock(&wq->lock);
}

static void lc_mutex_setup(void *object)
{
    struct mutex *lock = object;

    lc_nt_event_init(&lock->event, 1, 0);
}

void lc_mutex_init(struct mutex *lock)
{
    lock->owner = NULL;
    lock->state = 0;
    lc_mutex_setup(lock);
    __atomic_store_n(&lock->initialized, 1, __ATOMIC_RELEASE);
}

void lc_mutex_lock(struct mutex *lock)
{
    int expected = 0;

    if (!__atomic_compare_exchange_n(&lock->state, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        lc_once(&lock->initialized, lc_mutex_setup, lock);
        lc_work_batch_unplug();
        while (__atomic_exchange_n(&lock->state, 2, __ATOMIC_ACQUIRE) != 0)
            lc_nt_event_wait(&lock->event, -1);
    }
    __atomic_store_n(&lock->owner, lc_nt_current_thread(), __ATOMIC_RELAXED);
}

int lc_mutex_trylock(struct mutex *lock)
{
    int expected = 0;

    if (!__atomic_compare_exchange_n(&lock->state, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    __atomic_store_n(&lock->owner, lc_nt_current_thread(), __ATOMIC_RELAXED);
    return 1;
}

void lc_mutex_unlock(struct mutex *lock)
{
    WARN_ON(__atomic_load_n(&lock->owner, __ATOMIC_RELAXED) == NULL);
    __atomic_store_n(&lock->owner, NULL, __ATOMIC_RELAXED);
    if (__atomic_exchange_n(&lock->state, 0, __ATOMIC_RELEASE) == 2)
        lc_nt_event_set(&lock->event);
}

bool lc_mutex_is_locked(struct mutex *lock)
{
    return __atomic_load_n(&lock->owner, __ATOMIC_RELAXED) != NULL;
}

static DEFINE_SPINLOCK(lc_rcu_lock);
#define LC_RCU_BATCH_MS 10

static struct rcu_head *lc_rcu_pending;
static struct delayed_work lc_rcu_work;
static int lc_rcu_work_ready;
static DEFINE_MUTEX(lc_rcu_barrier_lock);

void lc_rcu_read_lock(void)
{
    lc_nt_rcu_read_lock();
}

void lc_rcu_read_unlock(void)
{
    lc_nt_rcu_read_unlock();
}

void lc_synchronize_rcu(void)
{
    lc_nt_rcu_synchronize();
}

static void lc_rcu_run(struct work_struct *work)
{
    struct rcu_head *batch, *next;

    (void)work;
    mutex_lock(&lc_rcu_barrier_lock);
    spin_lock(&lc_rcu_lock);
    batch = lc_rcu_pending;
    lc_rcu_pending = NULL;
    spin_unlock(&lc_rcu_lock);

    if (batch)
    {
        lc_nt_rcu_synchronize();
        for (; batch; batch = next)
        {
            next = batch->next;
            batch->func(batch);
        }
    }
    mutex_unlock(&lc_rcu_barrier_lock);
}

static void lc_rcu_work_setup(void *object)
{
    INIT_DELAYED_WORK(object, lc_rcu_run);
}

void lc_call_rcu(struct rcu_head *head, rcu_callback_t func)
{
    head->func = func;
    lc_once(&lc_rcu_work_ready, lc_rcu_work_setup, &lc_rcu_work);
    spin_lock(&lc_rcu_lock);
    head->next = lc_rcu_pending;
    lc_rcu_pending = head;
    spin_unlock(&lc_rcu_lock);
    queue_delayed_work(system_wq, &lc_rcu_work, msecs_to_jiffies(LC_RCU_BATCH_MS));
}

void lc_rcu_barrier(void)
{
    lc_once(&lc_rcu_work_ready, lc_rcu_work_setup, &lc_rcu_work);
    for (;;)
    {
        bool pending;

        flush_delayed_work(&lc_rcu_work);
        spin_lock(&lc_rcu_lock);
        pending = lc_rcu_pending != NULL;
        spin_unlock(&lc_rcu_lock);
        if (!pending)
            break;
        queue_delayed_work(system_wq, &lc_rcu_work, 0);
    }
}

void lc_kvfree_rcu_cb(struct rcu_head *head)
{
    kfree((char *)head - head->lc_offset);
}
