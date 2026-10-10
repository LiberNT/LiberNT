/*
 * PROJECT:     LiberNT Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux lists, atomics, locks and wait queues over NT primitives
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

struct mutex;
void lc_mutex_lock(struct mutex *lock);
void lc_mutex_unlock(struct mutex *lock);

struct list_head
{
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list) { list->next = list; list->prev = list; }
static inline void __list_add(struct list_head *n, struct list_head *prev, struct list_head *next)
{
    next->prev = n;
    n->next = next;
    n->prev = prev;
    prev->next = n;
}
static inline void list_add(struct list_head *n, struct list_head *head) { __list_add(n, head, head->next); }
static inline void list_add_tail(struct list_head *n, struct list_head *head) { __list_add(n, head->prev, head); }
static inline void __list_del(struct list_head *prev, struct list_head *next) { next->prev = prev; prev->next = next; }
static inline void list_del(struct list_head *entry) { __list_del(entry->prev, entry->next); entry->next = entry->prev = NULL; }
static inline void list_del_init(struct list_head *entry) { __list_del(entry->prev, entry->next); INIT_LIST_HEAD(entry); }
static inline void list_move(struct list_head *list, struct list_head *head) { __list_del(list->prev, list->next); list_add(list, head); }
static inline void list_move_tail(struct list_head *list, struct list_head *head) { __list_del(list->prev, list->next); list_add_tail(list, head); }
static inline bool list_empty(const struct list_head *head) { return READ_ONCE(head->next) == head; }
static inline bool list_is_last(const struct list_head *list, const struct list_head *head) { return list->next == head; }
static inline bool list_is_singular(const struct list_head *head) { return !list_empty(head) && head->next == head->prev; }
static inline void __list_splice(const struct list_head *list, struct list_head *prev, struct list_head *next)
{
    struct list_head *first = list->next, *last = list->prev;
    first->prev = prev;
    prev->next = first;
    last->next = next;
    next->prev = last;
}
static inline void list_splice_init(struct list_head *list, struct list_head *head)
{
    if (!list_empty(list))
    {
        __list_splice(list, head, head->next);
        INIT_LIST_HEAD(list);
    }
}
static inline void list_splice_tail_init(struct list_head *list, struct list_head *head)
{
    if (!list_empty(list))
    {
        __list_splice(list, head->prev, head);
        INIT_LIST_HEAD(list);
    }
}
static inline void list_splice(const struct list_head *list, struct list_head *head)
{
    if (!list_empty(list))
        __list_splice(list, head, head->next);
}
static inline void list_splice_tail(struct list_head *list, struct list_head *head)
{
    if (!list_empty(list))
        __list_splice(list, head->prev, head);
}
#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(ptr, type, member) list_entry((ptr)->next, type, member)
#define list_last_entry(ptr, type, member) list_entry((ptr)->prev, type, member)
#define list_first_entry_or_null(ptr, type, member) (!list_empty(ptr) ? list_first_entry(ptr, type, member) : NULL)
#define list_next_entry(pos, member) list_entry((pos)->member.next, typeof(*(pos)), member)
#define list_prev_entry(pos, member) list_entry((pos)->member.prev, typeof(*(pos)), member)
#define list_entry_is_head(pos, head, member) (&pos->member == (head))
#define list_for_each(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)
#define list_for_each_safe(pos, n, head) for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)
#define list_for_each_entry(pos, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member); !list_entry_is_head(pos, head, member); pos = list_next_entry(pos, member))
#define list_for_each_entry_reverse(pos, head, member) \
    for (pos = list_last_entry(head, typeof(*pos), member); !list_entry_is_head(pos, head, member); pos = list_prev_entry(pos, member))
#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member), n = list_next_entry(pos, member); \
         !list_entry_is_head(pos, head, member); pos = n, n = list_next_entry(n, member))
#define list_for_each_entry_safe_reverse(pos, n, head, member) \
    for (pos = list_last_entry(head, typeof(*pos), member), n = list_prev_entry(pos, member); \
         !list_entry_is_head(pos, head, member); pos = n, n = list_prev_entry(n, member))
#define list_for_each_prev(pos, head) for (pos = (head)->prev; pos != (head); pos = pos->prev)
#define list_count_nodes(head) ({ size_t __lc_n = 0; struct list_head *__lc_p; list_for_each(__lc_p, head) __lc_n++; __lc_n; })
#define list_for_each_entry_continue(pos, head, member) \
    for (pos = list_next_entry(pos, member); !list_entry_is_head(pos, head, member); pos = list_next_entry(pos, member))
#define list_for_each_entry_from(pos, head, member) \
    for (; !list_entry_is_head(pos, head, member); pos = list_next_entry(pos, member))

struct hlist_node { struct hlist_node *next, **pprev; };
struct hlist_head { struct hlist_node *first; };

typedef struct { int counter; } atomic_t;
typedef struct { s64 counter; } atomic64_t;
typedef atomic64_t atomic_long_t;
#define ATOMIC_INIT(i) { (i) }
#define ATOMIC64_INIT(i) { (i) }
static inline int atomic_read(const atomic_t *v) { return __atomic_load_n(&v->counter, __ATOMIC_RELAXED); }
static inline void atomic_set(atomic_t *v, int i) { __atomic_store_n(&v->counter, i, __ATOMIC_RELAXED); }
static inline void atomic_inc(atomic_t *v) { __atomic_fetch_add(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline void atomic_dec(atomic_t *v) { __atomic_fetch_sub(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline void atomic_add(int i, atomic_t *v) { __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline void atomic_sub(int i, atomic_t *v) { __atomic_fetch_sub(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_inc_return(atomic_t *v) { return __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline int atomic_dec_return(atomic_t *v) { return __atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline int atomic_add_return(int i, atomic_t *v) { return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline bool atomic_dec_and_test(atomic_t *v) { return atomic_dec_return(v) == 0; }
static inline int atomic_xchg(atomic_t *v, int n) { return __atomic_exchange_n(&v->counter, n, __ATOMIC_SEQ_CST); }
static inline int atomic_cmpxchg(atomic_t *v, int o, int n) { __atomic_compare_exchange_n(&v->counter, &o, n, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return o; }
static inline int atomic_fetch_or(int i, atomic_t *v) { return __atomic_fetch_or(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_fetch_and(int i, atomic_t *v) { return __atomic_fetch_and(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_fetch_inc(atomic_t *v) { return __atomic_fetch_add(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline s64 atomic64_read(const atomic64_t *v) { return __atomic_load_n(&v->counter, __ATOMIC_RELAXED); }
static inline void atomic64_set(atomic64_t *v, s64 i) { __atomic_store_n(&v->counter, i, __ATOMIC_RELAXED); }
static inline s64 atomic64_inc_return(atomic64_t *v) { return __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline s64 atomic64_add_return(s64 i, atomic64_t *v) { return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline void atomic64_inc(atomic64_t *v) { __atomic_fetch_add(&v->counter, 1, __ATOMIC_SEQ_CST); }
#define atomic_long_read atomic64_read
#define atomic_long_set atomic64_set
#define atomic_long_xchg atomic64_xchg
#define atomic_long_cmpxchg atomic64_cmpxchg
#define atomic_long_inc atomic64_inc
#define atomic_long_add_return atomic64_add_return
#define xchg(ptr, v) __atomic_exchange_n(ptr, v, __ATOMIC_SEQ_CST)
#define cmpxchg(ptr, o, n) ({ typeof(*(ptr)) __o = (o); __atomic_compare_exchange_n(ptr, &__o, n, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); __o; })

typedef struct { atomic_t refs; } refcount_t;
#define REFCOUNT_INIT(n) { .refs = ATOMIC_INIT(n) }
static inline void refcount_set(refcount_t *r, int n) { atomic_set(&r->refs, n); }
static inline unsigned int refcount_read(const refcount_t *r) { return (unsigned int)atomic_read(&r->refs); }
static inline void refcount_inc(refcount_t *r) { atomic_inc(&r->refs); }
static inline bool refcount_dec_and_test(refcount_t *r) { return atomic_dec_and_test(&r->refs); }
static inline bool refcount_inc_not_zero(refcount_t *r)
{
    int old = atomic_read(&r->refs);
    while (old)
    {
        if (__atomic_compare_exchange_n(&r->refs.counter, &old, old + 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return true;
    }
    return false;
}

struct kref { refcount_t refcount; };
#define KREF_INIT(n) { .refcount = REFCOUNT_INIT(n) }
static inline void kref_init(struct kref *kref) { refcount_set(&kref->refcount, 1); }
static inline unsigned int kref_read(const struct kref *kref) { return refcount_read(&kref->refcount); }
static inline void kref_get(struct kref *kref) { refcount_inc(&kref->refcount); }
static inline int kref_put(struct kref *kref, void (*release)(struct kref *kref))
{
    if (refcount_dec_and_test(&kref->refcount))
    {
        release(kref);
        return 1;
    }
    return 0;
}
static inline bool kref_get_unless_zero(struct kref *kref) { return refcount_inc_not_zero(&kref->refcount); }


static inline s64 atomic64_fetch_add(s64 i, atomic64_t *v) { return __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline s64 atomic64_xchg(atomic64_t *v, s64 n) { return __atomic_exchange_n(&v->counter, n, __ATOMIC_SEQ_CST); }
static inline s64 atomic64_cmpxchg(atomic64_t *v, s64 o, s64 n) { __atomic_compare_exchange_n(&v->counter, &o, n, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return o; }
static inline bool atomic_inc_not_zero(atomic_t *v)
{
    int old = atomic_read(v);
    while (old)
    {
        if (__atomic_compare_exchange_n(&v->counter, &old, old + 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return true;
    }
    return false;
}
static inline bool atomic_try_cmpxchg(atomic_t *v, int *old, int n)
{
    return __atomic_compare_exchange_n(&v->counter, old, n, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#define try_cmpxchg(ptr, oldp, n) __atomic_compare_exchange_n(ptr, oldp, n, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

typedef struct spinlock
{
    lc_nt_spinlock nt;
} spinlock_t;
typedef spinlock_t raw_spinlock_t;
#define __SPIN_LOCK_UNLOCKED(name) { { { 0 } } }
#define DEFINE_SPINLOCK(x) spinlock_t x = __SPIN_LOCK_UNLOCKED(x)
static inline void spin_lock_init(spinlock_t *lock) { lc_nt_spin_init(&lock->nt); }
static inline void spin_lock(spinlock_t *lock) { lc_nt_spin_acquire(&lock->nt); }
static inline void spin_unlock(spinlock_t *lock) { lc_nt_spin_release(&lock->nt); }
static inline int spin_trylock(spinlock_t *lock) { lc_nt_spin_acquire(&lock->nt); return 1; }
#define spin_lock_irq(l) spin_lock(l)
#define spin_unlock_irq(l) spin_unlock(l)
#define spin_lock_bh(l) spin_lock(l)
#define spin_unlock_bh(l) spin_unlock(l)
#define spin_lock_irqsave(l, flags) do { (flags) = 0; spin_lock(l); } while (0)
#define spin_unlock_irqrestore(l, flags) do { (void)(flags); spin_unlock(l); } while (0)
#define spin_lock_nested(l, s) spin_lock(l)
#define raw_spin_lock_init spin_lock_init
#define raw_spin_lock spin_lock
#define raw_spin_unlock spin_unlock
#define raw_spin_lock_irqsave spin_lock_irqsave
#define raw_spin_unlock_irqrestore spin_unlock_irqrestore
#define assert_spin_locked(l) do { (void)(l); } while (0)
#define lockdep_assert_held(l) do { (void)(l); } while (0)
#define lockdep_assert_not_held(l) do { (void)(l); } while (0)
#define lockdep_assert_held_write(l) do { (void)(l); } while (0)
#define lockdep_assert_held_once(l) do { (void)(l); } while (0)
#define lockdep_assert(c) do { (void)sizeof(c); } while (0)
#define lockdep_assert_once(c) do { (void)sizeof(c); } while (0)
#define lockdep_assert_none_held_once() do { } while (0)
#define lockdep_set_class(l, k) do { } while (0)
#define lockdep_init_map(m, n, k, s) do { } while (0)
#define lock_map_acquire(m) do { } while (0)
#define lock_map_release(m) do { } while (0)
#define lockdep_is_held(l) 1
#define might_sleep() do { } while (0)
#define might_lock(l) do { } while (0)
#define cond_resched() do { } while (0)
struct lock_class_key { int dummy; };
struct lockdep_map { int dummy; };

static inline bool refcount_dec_and_mutex_lock_lc(refcount_t *r, struct mutex *lock)
{
    int old = atomic_read(&r->refs);
    while (old > 1)
    {
        if (__atomic_compare_exchange_n(&r->refs.counter, &old, old - 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return false;
    }
    lc_mutex_lock(lock);
    if (refcount_dec_and_test(r))
        return true;
    lc_mutex_unlock(lock);
    return false;
}
#define refcount_dec_and_mutex_lock refcount_dec_and_mutex_lock_lc
static inline bool refcount_dec_and_lock(refcount_t *r, struct spinlock *lock)
{
    int old = atomic_read(&r->refs);
    while (old > 1)
    {
        if (__atomic_compare_exchange_n(&r->refs.counter, &old, old - 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return false;
    }
    spin_lock(lock);
    if (refcount_dec_and_test(r))
        return true;
    spin_unlock(lock);
    return false;
}

struct lc_waiter
{
    struct list_head node;
    lc_nt_event event;
    bool queued;
};

typedef struct wait_queue_head
{
    spinlock_t lock;
    struct list_head head;
    int initialized;
} wait_queue_head_t;

void lc_init_waitqueue_head(wait_queue_head_t *wq);
void lc_wake_up_all(wait_queue_head_t *wq);
void lc_prepare_to_wait(wait_queue_head_t *wq, struct lc_waiter *waiter);
void lc_finish_wait(wait_queue_head_t *wq, struct lc_waiter *waiter);
long lc_waiter_sleep(struct lc_waiter *waiter, long timeout);
static inline bool waitqueue_active(wait_queue_head_t *wq) { return wq->initialized && !list_empty(&wq->head); }
#define DECLARE_WAIT_QUEUE_HEAD(name) wait_queue_head_t name = { __SPIN_LOCK_UNLOCKED(name), { NULL, NULL }, 0 }
#define init_waitqueue_head(wq) lc_init_waitqueue_head(wq)
#define wake_up(wq) lc_wake_up_all(wq)
#define wake_up_all(wq) lc_wake_up_all(wq)
#define wake_up_interruptible(wq) lc_wake_up_all(wq)
#define wake_up_interruptible_all(wq) lc_wake_up_all(wq)

#define __lc_wait_event(wq, condition, timeout) ({                         \
    long __lc_ret = (long)(timeout);                                        \
    struct lc_waiter __lc_w;                                                \
    __lc_w.queued = false;                                                  \
    for (;;) {                                                              \
        lc_prepare_to_wait(&(wq), &__lc_w);                                 \
        if (condition)                                                      \
            break;                                                          \
        if (__lc_ret <= 0)                                                  \
            break;                                                          \
        __lc_ret = lc_waiter_sleep(&__lc_w, __lc_ret);                      \
    }                                                                       \
    lc_finish_wait(&(wq), &__lc_w);                                         \
    if ((condition) && __lc_ret <= 0)                                       \
        __lc_ret = 1;                                                       \
    else if (!(condition))                                                  \
        __lc_ret = 0;                                                       \
    __lc_ret; })
#define wait_event_timeout(wq, condition, timeout) __lc_wait_event(wq, condition, timeout)
#define wait_event_interruptible_timeout(wq, condition, timeout) __lc_wait_event(wq, condition, timeout)
#define wait_event_killable_timeout(wq, condition, timeout) __lc_wait_event(wq, condition, timeout)
#define wait_event(wq, condition) do { (void)__lc_wait_event(wq, condition, MAX_SCHEDULE_TIMEOUT); } while (0)
#define wait_event_interruptible(wq, condition) ({ wait_event(wq, condition); 0; })
#define wait_event_killable(wq, condition) ({ wait_event(wq, condition); 0; })
#define wait_event_lock_irq(wq, condition, lock) do { spin_unlock(&(lock)); wait_event(wq, condition); spin_lock(&(lock)); } while (0)

struct mutex
{
    lc_nt_event event;
    void *owner;
    int initialized;
    int state;
};
void lc_mutex_init(struct mutex *lock);
void lc_mutex_lock(struct mutex *lock);
int lc_mutex_trylock(struct mutex *lock);
void lc_mutex_unlock(struct mutex *lock);
bool lc_mutex_is_locked(struct mutex *lock);
#define mutex_init(l) lc_mutex_init(l)
#define __mutex_init(l, n, k) lc_mutex_init(l)
#define mutex_destroy(l) do { (void)(l); } while (0)
#define mutex_lock(l) lc_mutex_lock(l)
#define mutex_lock_interruptible(l) (lc_mutex_lock(l), 0)
#define mutex_lock_killable(l) (lc_mutex_lock(l), 0)
#define mutex_lock_nested(l, s) lc_mutex_lock(l)
#define mutex_trylock(l) lc_mutex_trylock(l)
#define mutex_unlock(l) lc_mutex_unlock(l)
#define mutex_is_locked(l) lc_mutex_is_locked(l)
#define DEFINE_MUTEX(name) struct mutex name = { { { 0 } }, NULL, 0, 0 }

struct rw_semaphore { struct mutex lock; };
#define init_rwsem(s) lc_mutex_init(&(s)->lock)
#define down_read(s) lc_mutex_lock(&(s)->lock)
#define up_read(s) lc_mutex_unlock(&(s)->lock)
#define down_write(s) lc_mutex_lock(&(s)->lock)
#define up_write(s) lc_mutex_unlock(&(s)->lock)
#define down_read_trylock(s) lc_mutex_trylock(&(s)->lock)
#define down_write_trylock(s) lc_mutex_trylock(&(s)->lock)
#define down_read_interruptible(s) (lc_mutex_lock(&(s)->lock), 0)
#define downgrade_write(s) do { (void)(s); } while (0)
#define lockdep_assert_held_read(s) do { (void)(s); } while (0)

struct completion
{
    unsigned int done;
    wait_queue_head_t wait;
};
static inline void init_completion(struct completion *x) { x->done = 0; lc_init_waitqueue_head(&x->wait); }
static inline void reinit_completion(struct completion *x) { x->done = 0; }
static inline void complete(struct completion *x) { __atomic_fetch_add(&x->done, 1, __ATOMIC_SEQ_CST); lc_wake_up_all(&x->wait); }
static inline void complete_all(struct completion *x) { __atomic_store_n(&x->done, UINT_MAX / 2, __ATOMIC_SEQ_CST); lc_wake_up_all(&x->wait); }
static inline bool lc_completion_try(struct completion *x)
{
    unsigned int done = __atomic_load_n(&x->done, __ATOMIC_ACQUIRE);
    while (done)
    {
        if (done >= UINT_MAX / 2)
            return true;
        if (__atomic_compare_exchange_n(&x->done, &done, done - 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return true;
    }
    return false;
}
static inline unsigned long wait_for_completion_timeout(struct completion *x, unsigned long timeout)
{
    return (unsigned long)wait_event_timeout(x->wait, lc_completion_try(x), (long)timeout);
}
static inline void wait_for_completion(struct completion *x) { wait_event(x->wait, lc_completion_try(x)); }
static inline int wait_for_completion_interruptible(struct completion *x) { wait_for_completion(x); return 0; }
static inline bool completion_done(struct completion *x) { return READ_ONCE(x->done) != 0; }

#define __LC_CAT2(a, b) a##b
#define __LC_CAT(a, b) __LC_CAT2(a, b)
#define __LC_UNIQUE(p) __LC_CAT(p, __COUNTER__)
#define DEFINE_CLASS(_name, _type, _exit, _init, _init_args...)                          \
    typedef _type class_##_name##_t;                                                       \
    static inline void class_##_name##_destructor(_type *p) { _type _T = *p; _exit; }     \
    static inline _type class_##_name##_constructor(_init_args) { _type t = _init; return t; }
#define CLASS(_name, var) \
    class_##_name##_t var __attribute__((cleanup(class_##_name##_destructor))) = class_##_name##_constructor
#define DEFINE_GUARD(_name, _type, _lock, _unlock)                                        \
    DEFINE_CLASS(_name, _type, if (_T) { _unlock; }, ({ _lock; _T; }), _type _T)          \
    static inline void *class_##_name##_lock_ptr(class_##_name##_t *_T) { return (void *)*_T; }
#define guard(_name) CLASS(_name, __LC_UNIQUE(__lc_guard))
#define scoped_guard(_name, args...)                                                      \
    for (CLASS(_name, __lc_scope)(args), *__lc_scope_done = NULL; !__lc_scope_done;        \
         __lc_scope_done = (void *)&__lc_scope)
DEFINE_GUARD(mutex, struct mutex *, lc_mutex_lock(_T), lc_mutex_unlock(_T))
DEFINE_GUARD(spinlock, spinlock_t *, spin_lock(_T), spin_unlock(_T))
DEFINE_GUARD(spinlock_irq, spinlock_t *, spin_lock(_T), spin_unlock(_T))
DEFINE_GUARD(spinlock_irqsave, spinlock_t *, spin_lock(_T), spin_unlock(_T))
DEFINE_GUARD(rwsem_read, struct rw_semaphore *, lc_mutex_lock(&_T->lock), lc_mutex_unlock(&_T->lock))
DEFINE_GUARD(rwsem_write, struct rw_semaphore *, lc_mutex_lock(&_T->lock), lc_mutex_unlock(&_T->lock))
#define __free(f) __attribute__((cleanup(lc_free_##f)))
#define no_free_ptr(p) ({ typeof(p) __p = (p); (p) = NULL; __p; })
#define return_ptr(p) return no_free_ptr(p)
#define DEFINE_FREE(name, type, free) static inline void lc_free_##name(void *p) { type _T = *(type *)p; free; }

static inline int kref_put_mutex(struct kref *kref, void (*release)(struct kref *kref), struct mutex *lock)
{
    if (refcount_dec_and_mutex_lock_lc(&kref->refcount, lock))
    {
        release(kref);
        return 1;
    }
    return 0;
}
static inline int kref_put_lock(struct kref *kref, void (*release)(struct kref *kref), spinlock_t *lock)
{
    if (refcount_dec_and_lock(&kref->refcount, lock))
    {
        release(kref);
        return 1;
    }
    return 0;
}
