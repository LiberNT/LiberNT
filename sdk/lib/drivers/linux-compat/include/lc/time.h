/*
 * PROJECT:     LiberNT Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux time, delays, polling, workqueues and timers over NT primitives
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

static inline unsigned long lc_jiffies(void) { return (unsigned long)(lc_nt_time_ns() / 1000000ULL); }
#define jiffies lc_jiffies()
#define jiffies_64 ((u64)lc_jiffies())
#define get_jiffies_64() ((u64)lc_jiffies())
static inline unsigned long msecs_to_jiffies(unsigned int m) { return m; }
static inline unsigned long usecs_to_jiffies(unsigned int u) { return (u + 999U) / 1000U; }
static inline unsigned long nsecs_to_jiffies(u64 n) { return (unsigned long)(n / 1000000ULL); }
static inline u64 nsecs_to_jiffies64(u64 n) { return n / 1000000ULL; }
static inline u64 jiffies64_to_nsecs(u64 j) { return j * 1000000ULL; }
static inline unsigned int jiffies_to_msecs(unsigned long j) { return (unsigned int)j; }
static inline unsigned int jiffies_to_usecs(unsigned long j) { return (unsigned int)(j * 1000U); }
#define time_after(a, b) ((long)((b) - (a)) < 0)
#define time_before(a, b) time_after(b, a)
#define time_after_eq(a, b) ((long)((a) - (b)) >= 0)
#define time_before_eq(a, b) time_after_eq(b, a)
#define time_is_before_jiffies(a) time_after(jiffies, a)
#define time_is_after_jiffies(a) time_before(jiffies, a)

static inline ktime_t ktime_get(void) { return (ktime_t)lc_nt_time_ns(); }
#define ktime_get_raw() ktime_get()
#define ktime_get_boottime() ktime_get()
#define ktime_get_mono_fast_ns() ((u64)ktime_get())
#define ktime_get_ns() ((u64)ktime_get())
#define ktime_get_raw_ns() ((u64)ktime_get())
static inline ktime_t ktime_set(s64 secs, unsigned long nsecs) { return secs * NSEC_PER_SEC + (s64)nsecs; }
static inline ktime_t ktime_add(ktime_t a, ktime_t b) { return a + b; }
static inline ktime_t ktime_sub(ktime_t a, ktime_t b) { return a - b; }
static inline ktime_t ktime_add_ns(ktime_t a, u64 ns) { return a + (s64)ns; }
static inline ktime_t ktime_add_us(ktime_t a, u64 us) { return a + (s64)us * NSEC_PER_USEC; }
static inline ktime_t ktime_add_ms(ktime_t a, u64 ms) { return a + (s64)ms * NSEC_PER_MSEC; }
static inline ktime_t ktime_sub_ns(ktime_t a, u64 ns) { return a - (s64)ns; }
static inline s64 ktime_to_ns(ktime_t t) { return t; }
static inline s64 ktime_to_us(ktime_t t) { return t / NSEC_PER_USEC; }
static inline s64 ktime_to_ms(ktime_t t) { return t / NSEC_PER_MSEC; }
static inline ktime_t ns_to_ktime(u64 ns) { return (ktime_t)ns; }
static inline ktime_t us_to_ktime(u64 us) { return (ktime_t)us * NSEC_PER_USEC; }
static inline ktime_t ms_to_ktime(u64 ms) { return (ktime_t)ms * NSEC_PER_MSEC; }
static inline int ktime_compare(ktime_t a, ktime_t b) { return a < b ? -1 : a > b ? 1 : 0; }
static inline bool ktime_after(ktime_t a, ktime_t b) { return a > b; }
static inline bool ktime_before(ktime_t a, ktime_t b) { return a < b; }
static inline s64 ktime_us_delta(ktime_t later, ktime_t earlier) { return ktime_to_us(later - earlier); }
static inline s64 ktime_ms_delta(ktime_t later, ktime_t earlier) { return ktime_to_ms(later - earlier); }
#define KTIME_MAX ((ktime_t)S64_MAX)

static inline void udelay(unsigned long us) { lc_nt_stall_us((uint32_t)us); }
static inline void ndelay(unsigned long ns) { lc_nt_stall_us((uint32_t)((ns + 999) / 1000)); }
static inline void mdelay(unsigned long ms) { while (ms--) lc_nt_stall_us(1000); }
static inline void msleep(unsigned int ms) { lc_nt_sleep_100ns((int64_t)ms * 10000); }
static inline void usleep_range(unsigned long min, unsigned long max) { (void)max; lc_nt_sleep_100ns((int64_t)min * 10); }
static inline void fsleep(unsigned long us) { lc_nt_sleep_100ns((int64_t)us * 10); }
static inline unsigned long msleep_interruptible(unsigned int ms) { msleep(ms); return 0; }
#define TASK_RUNNING 0x0000
#define TASK_INTERRUPTIBLE 0x0001
#define TASK_UNINTERRUPTIBLE 0x0002
#define TASK_KILLABLE 0x0102
struct task_struct;
long lc_schedule_timeout(long timeout);
int lc_wake_up_process(struct task_struct *task);
#define schedule_timeout(t) lc_schedule_timeout(t)
#define schedule_timeout_uninterruptible(t) lc_schedule_timeout(t)
#define schedule_timeout_interruptible(t) lc_schedule_timeout(t)
#define schedule() ((void)lc_schedule_timeout(MAX_SCHEDULE_TIMEOUT))
#define wake_up_process(t) lc_wake_up_process(t)
#define set_current_state(s) do { (void)(s); } while (0)
#define __set_current_state(s) do { (void)(s); } while (0)
static inline void cpu_relax(void) { __asm__ __volatile__("" ::: "memory"); }

#define read_poll_timeout(op, val, cond, sleep_us, timeout_us, sleep_before_read, args...) ({ \
    u64 __lc_timeout_us = (timeout_us);                                                     \
    unsigned long __lc_sleep_us = (sleep_us);                                               \
    ktime_t __lc_timeout = ktime_add_us(ktime_get(), __lc_timeout_us);                      \
    if ((sleep_before_read) && __lc_sleep_us)                                               \
        usleep_range(__lc_sleep_us, __lc_sleep_us);                                         \
    for (;;) {                                                                              \
        (val) = op(args);                                                                   \
        if (cond)                                                                           \
            break;                                                                          \
        if (__lc_timeout_us && ktime_compare(ktime_get(), __lc_timeout) > 0) {              \
            (val) = op(args);                                                               \
            break;                                                                          \
        }                                                                                   \
        if (__lc_sleep_us)                                                                  \
            usleep_range(__lc_sleep_us, __lc_sleep_us);                                     \
        cpu_relax();                                                                        \
    }                                                                                       \
    (cond) ? 0 : -ETIMEDOUT; })
#define read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us, delay_before_read, args...) ({ \
    u64 __lc_timeout_us = (timeout_us);                                                     \
    unsigned long __lc_delay_us = (delay_us);                                               \
    ktime_t __lc_timeout = ktime_add_us(ktime_get(), __lc_timeout_us);                      \
    if ((delay_before_read) && __lc_delay_us)                                               \
        udelay(__lc_delay_us);                                                              \
    for (;;) {                                                                              \
        (val) = op(args);                                                                   \
        if (cond)                                                                           \
            break;                                                                          \
        if (__lc_timeout_us && ktime_compare(ktime_get(), __lc_timeout) > 0) {              \
            (val) = op(args);                                                               \
            break;                                                                          \
        }                                                                                   \
        if (__lc_delay_us)                                                                  \
            udelay(__lc_delay_us);                                                          \
        cpu_relax();                                                                        \
    }                                                                                       \
    (cond) ? 0 : -ETIMEDOUT; })
#define readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us) \
    read_poll_timeout(op, val, cond, sleep_us, timeout_us, false, addr)
#define readl_poll_timeout(addr, val, cond, delay_us, timeout_us) \
    readx_poll_timeout(readl, addr, val, cond, delay_us, timeout_us)
#define readq_poll_timeout(addr, val, cond, delay_us, timeout_us) \
    readx_poll_timeout(readq, addr, val, cond, delay_us, timeout_us)
#define readl_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
    read_poll_timeout_atomic(readl, val, cond, delay_us, timeout_us, false, addr)

struct work_struct;
struct workqueue_struct;
typedef void (*work_func_t)(struct work_struct *work);

struct work_struct
{
    struct list_head entry;
    work_func_t func;
    unsigned long state;
    struct workqueue_struct *wq;
};

struct timer_list
{
    lc_nt_timer nt;
    void (*function)(struct timer_list *timer);
    unsigned long expires;
    int initialized;
};

struct delayed_work
{
    struct work_struct work;
    struct timer_list timer;
    struct workqueue_struct *wq;
    int timer_armed;
};

#define WQ_UNBOUND (1U << 1)
#define WQ_FREEZABLE (1U << 2)
#define WQ_MEM_RECLAIM (1U << 3)
#define WQ_HIGHPRI (1U << 4)
#define WQ_CPU_INTENSIVE (1U << 5)
#define WQ_SYSFS (1U << 6)
#define WQ_POWER_EFFICIENT (1U << 7)
#define __WQ_ORDERED (1U << 17)
#define WQ_MAX_ACTIVE 512
#define WORK_STRUCT_PENDING_BIT 0
#define WORK_STRUCT_INIT_BIT 1

void lc_init_work(struct work_struct *work, work_func_t func);
void lc_init_delayed_work(struct delayed_work *dwork, work_func_t func);
struct workqueue_struct *lc_alloc_workqueue(const char *name, unsigned int flags, int max_active);
void lc_destroy_workqueue(struct workqueue_struct *wq);
bool lc_queue_work(struct workqueue_struct *wq, struct work_struct *work);
bool lc_queue_work_tail(struct workqueue_struct *wq, struct work_struct *work);
void lc_work_batch_begin(void);
void lc_work_batch_end(void);
void lc_work_batch_unplug(void);
bool lc_queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay);
bool lc_mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay);
bool lc_cancel_work(struct work_struct *work);
bool lc_cancel_work_sync(struct work_struct *work);
bool lc_cancel_delayed_work(struct delayed_work *dwork);
bool lc_cancel_delayed_work_sync(struct delayed_work *dwork);
bool lc_flush_work(struct work_struct *work);
bool lc_flush_delayed_work(struct delayed_work *dwork);
void lc_flush_workqueue(struct workqueue_struct *wq);
bool lc_work_pending(struct work_struct *work);
struct work_struct *lc_current_work(void);
struct workqueue_struct *lc_system_wq(void);
#define system_wq lc_system_wq()
#define system_unbound_wq lc_system_wq()
#define system_highpri_wq lc_system_wq()
#define system_long_wq lc_system_wq()
#define system_percpu_wq lc_system_wq()
#define system_dfl_wq lc_system_wq()

#define INIT_WORK(w, f) lc_init_work(w, f)
#define INIT_DELAYED_WORK(w, f) lc_init_delayed_work(w, f)
#define INIT_WORK_ONSTACK(w, f) lc_init_work(w, f)
#define destroy_work_on_stack(w) do { (void)(w); } while (0)
#define alloc_workqueue(fmt, flags, max_active, ...) lc_alloc_workqueue(fmt, flags, max_active)
#define alloc_ordered_workqueue(fmt, flags, ...) lc_alloc_workqueue(fmt, (flags) | __WQ_ORDERED, 1)
#define alloc_ordered_workqueue_lockdep_map(fmt, flags, map, ...) lc_alloc_workqueue(fmt, (flags) | __WQ_ORDERED, 1)
#define destroy_workqueue(wq) lc_destroy_workqueue(wq)
#define queue_work(wq, w) lc_queue_work(wq, w)
#define queue_delayed_work(wq, w, d) lc_queue_delayed_work(wq, w, d)
#define mod_delayed_work(wq, w, d) lc_mod_delayed_work(wq, w, d)
#define schedule_work(w) lc_queue_work(system_wq, w)
#define schedule_delayed_work(w, d) lc_queue_delayed_work(system_wq, w, d)
#define cancel_work(w) lc_cancel_work(w)
#define cancel_work_sync(w) lc_cancel_work_sync(w)
#define cancel_delayed_work(w) lc_cancel_delayed_work(w)
#define cancel_delayed_work_sync(w) lc_cancel_delayed_work_sync(w)
#define disable_work_sync(w) lc_cancel_work_sync(w)
#define flush_work(w) lc_flush_work(w)
#define flush_delayed_work(w) lc_flush_delayed_work(w)
#define flush_workqueue(wq) lc_flush_workqueue(wq)
#define drain_workqueue(wq) lc_flush_workqueue(wq)
#define work_pending(w) lc_work_pending(w)
#define delayed_work_pending(w) lc_work_pending(&(w)->work)
#define current_work() lc_current_work()
static inline struct delayed_work *to_delayed_work(struct work_struct *work) { return container_of(work, struct delayed_work, work); }

void lc_timer_setup(struct timer_list *timer, void (*function)(struct timer_list *), unsigned int flags);
int lc_mod_timer(struct timer_list *timer, unsigned long expires);
int lc_del_timer(struct timer_list *timer);
#define timer_setup(t, f, flags) lc_timer_setup(t, f, flags)
#define mod_timer(t, e) lc_mod_timer(t, e)
#define del_timer(t) lc_del_timer(t)
#define del_timer_sync(t) lc_del_timer(t)
#define timer_delete(t) lc_del_timer(t)
#define timer_delete_sync(t) lc_del_timer(t)
#define from_timer(var, callback_timer, timer_fieldname) container_of(callback_timer, typeof(*var), timer_fieldname)
#define timer_container_of(var, callback_timer, timer_fieldname) container_of(callback_timer, typeof(*var), timer_fieldname)
