/*
 * Tiku Drivers - ESP32-C61 radio libraries, the OS they run on
 *
 * Espressif's libraries call an OS through one table of functions.  Here it
 * is tikuOS: blocking objects over kernel wait queues, tasks as worker
 * threads, timers on SYSTIMER's driver alarm with a thread for callbacks,
 * the libraries' interrupt lines remapped onto the radio's CLIC lines, and
 * their heap.  Coexistence, sleep retention and NVS are not used: inert.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hal/tiku_cpu.h>
#include <kernel/threads/tiku_thread.h>
#include <kernel/timers/tiku_clock.h>
#include "esp_arch.h"

#include "esp_heap.h"
#include "esp_port.h"
#if ESPW_COEX
#include "esp_coex_abi.h"
#endif

/* The libraries count time in their own ticks: here, milliseconds. */
#define OSI_MAX_PRIORITY    25
#define OSI_TASK_MARGIN     1024U       /* stack the adapter's own frames use */
#define OSI_TIMER_STACK     3072U
#define OSI_LIB_LINES       (ESPW_ARCH_LINES_RADIO - 1U)
#define COUNTS_PER_US       (ESPW_ARCH_SYSTIMER_HZ / 1000000UL)

/* Bring-up trace: tasks, interrupt routing, and every wait that blocks. */
#ifndef ESPW_TRACE
#define ESPW_TRACE 0
#endif
#if ESPW_TRACE
#define TRACE(...) do { if (!espw_arch_in_isr()) ESPW_PRINTF(__VA_ARGS__); } while (0)
#else
#define TRACE(...) do { } while (0)
#endif

#if ESPW_TRACE
static const char *who(void) {
    tiku_thread_t *t = tiku_thread_self();

    return t != NULL ? t->name : "kernel";
}
#endif

/** @brief Every 64th kernel wait: where each worker's saved context is. */
static void trace_workers(void) {
#if ESPW_TRACE
    static uint32_t n;

    if (tiku_thread_self() != NULL || (n++ & 63U) != 0U) {
        return;
    }
    for (uint8_t i = 0U; i < tiku_thread_count(); i++) {
        tiku_thread_t *t = tiku_thread_get(i);

        if (t != NULL && t->sp != NULL) {
            ESPW_PRINTF("  %s: state %u pc 0x%08lx ra 0x%08lx\n", t->name,
                        (unsigned)t->state,
                        (unsigned long)t->sp[ESPW_ARCH_F_MEPC],
                        (unsigned long)t->sp[ESPW_ARCH_F_RA]);
        }
    }
#endif
}

/*---------------------------------------------------------------------------*/
/* Waiting                                                                   */
/*---------------------------------------------------------------------------*/

static void osi_wait_mark(tiku_waitq_t *q);

void espw_deadline_start(espw_deadline_t *d, uint32_t ms) {
    unsigned long t = (unsigned long)(((uint64_t)ms * TIKU_CLOCK_SECOND +
                                       999U) / 1000U);

    d->forever = (uint8_t)(ms == OSI_FUNCS_TIME_BLOCKING);
    d->until = (tiku_clock_time_t)(tiku_clock_time() + (t != 0UL ? t : 1UL));
}

int espw_deadline_wait(tiku_waitq_t *q, espw_deadline_t *d, uint32_t ms) {
    tiku_clock_time_t now;
    int woke;

    if (ms == 0U || espw_arch_in_isr()) {
        return 0;
    }
    TRACE("wait: %s on 0x%08lx, %ld ms\n", who(), (unsigned long)(uintptr_t)q,
          d->forever ? -1L : (long)ms);
    trace_workers();
    if (d->forever) {
        osi_wait_mark(q);
        (void)tiku_thread_wait(q, 0UL);
        osi_wait_mark(NULL);
        return 1;
    }
    now = tiku_clock_time();
    if (!TIKU_CLOCK_LT(now, d->until)) {
        return 0;
    }
    osi_wait_mark(q);
    woke = tiku_thread_wait(q, (unsigned long)(tiku_clock_time_t)
                                (d->until - now));
    osi_wait_mark(NULL);
    return woke;
}

/** @brief Who is asking: a worker, or a stand-in for the kernel thread. */
static uint8_t osi_kernel_self;

void *espw_self(void) {
    tiku_thread_t *t = tiku_thread_self();

    return t != NULL ? (void *)t : (void *)&osi_kernel_self;
}

/*---------------------------------------------------------------------------*/
/* Semaphores and mutexes                                                    */
/*---------------------------------------------------------------------------*/

typedef struct {
    volatile uint32_t count;
    uint32_t          max;
    tiku_waitq_t      wq;
} osi_sem_t;

static void *osi_semphr_create(uint32_t max, uint32_t init) {
    osi_sem_t *s = espw_obj_alloc(sizeof *s);

    if (s != NULL) {
        s->max = max;
        s->count = init;
    }
    return s;
}

static void osi_semphr_delete(void *h) {
    espw_obj_free(h);
}

static int32_t osi_semphr_take(void *h, uint32_t ms) {
    osi_sem_t *s = h;
    espw_deadline_t d;
    int32_t got = 0;

    espw_deadline_start(&d, ms);
    tiku_atomic_enter();
    for (;;) {
        if (s->count > 0U) {
            s->count--;
            got = 1;
            break;
        }
        if (!espw_deadline_wait(&s->wq, &d, ms)) {
            break;
        }
    }
    tiku_atomic_exit();
    return got;
}

static int32_t osi_semphr_give(void *h) {
    osi_sem_t *s = h;
    int32_t ok = 0;

    tiku_atomic_enter();
    if (s->count < s->max) {
        s->count++;
        tiku_thread_wake_one(&s->wq);
        ok = 1;
    }
    tiku_atomic_exit();
    return ok;
}

/* One semaphore per thread, for the libraries' calls that wait on a reply;
 * kept from one start to the next, as the heap they live in is. */
static osi_sem_t *osi_thread_sem[TIKU_THREADS_MAX + 1U];

static void *osi_thread_semphr_get(void) {
    tiku_thread_t *t = tiku_thread_self();
    unsigned i = (t != NULL) ? t->slot : TIKU_THREADS_MAX;

    if (osi_thread_sem[i] == NULL) {
        osi_thread_sem[i] = osi_semphr_create(1U, 0U);
    }
    return osi_thread_sem[i];
}

typedef struct {
    void         *owner;
    uint32_t      depth;
    tiku_waitq_t  wq;
} osi_mutex_t;

static void *osi_mutex_create(void) {
    void *m = espw_obj_alloc(sizeof(osi_mutex_t));

    TRACE("mutex 0x%08lx made by %s\n", (unsigned long)(uintptr_t)m, who());
    return m;
}

static void osi_mutex_delete(void *h) {
    TRACE("mutex 0x%08lx deleted\n", (unsigned long)(uintptr_t)h);
    espw_obj_free(h);
}

/* Both kinds nest, as IDF's adapter takes them recursively either way. */
static int32_t osi_mutex_lock(void *h) {
    osi_mutex_t *m = h;
    void *me = espw_self();

    tiku_atomic_enter();
    while (m->depth != 0U && m->owner != me) {
        TRACE("wait: %s on mutex 0x%08lx\n", who(), (unsigned long)(uintptr_t)m);
        osi_wait_mark(&m->wq);
        (void)tiku_thread_wait(&m->wq, 0UL);
        osi_wait_mark(NULL);
    }
    m->owner = me;
    m->depth++;
    tiku_atomic_exit();
    return 1;
}

static int32_t osi_mutex_unlock(void *h) {
    osi_mutex_t *m = h;
    int32_t ok = 0;

    tiku_atomic_enter();
    if (m->depth != 0U && m->owner == espw_self()) {
        if (--m->depth == 0U) {
            m->owner = NULL;
            tiku_thread_wake_one(&m->wq);
        }
        ok = 1;
    }
    tiku_atomic_exit();
    return ok;
}

/*---------------------------------------------------------------------------*/
/* Queues and event groups                                                   */
/*---------------------------------------------------------------------------*/

typedef struct {
    uint8_t      *buf;
    uint32_t      item;
    uint32_t      len;
    uint32_t      head;
    volatile uint32_t count;
    tiku_waitq_t  rx_wq;
    tiku_waitq_t  tx_wq;
} osi_queue_t;

/* What _wifi_create_queue() hands back: the handle first, as IDF's is. */
typedef struct {
    void *handle;
    void *storage;
} osi_static_queue_t;

static void *osi_queue_create(uint32_t len, uint32_t item) {
    osi_queue_t *q = espw_calloc(1U, sizeof *q + len * item);

    if (q != NULL) {
        q->buf = (uint8_t *)(q + 1);
        q->item = item;
        q->len = len;
    }
    return q;
}

static void osi_queue_delete(void *h) {
    espw_free(h);
}

/** @brief Put @p item in, at the front or the back; with the lock held. */
static void queue_put(osi_queue_t *q, const void *item, int front) {
    uint32_t slot;

    if (front) {
        q->head = (q->head + q->len - 1U) % q->len;
        slot = q->head;
    } else {
        slot = (q->head + q->count) % q->len;
    }
    memcpy(q->buf + slot * q->item, item, q->item);
    q->count++;
    tiku_thread_wake_one(&q->rx_wq);
}

static int32_t queue_send(void *h, void *item, uint32_t ms, int front) {
    osi_queue_t *q = h;
    espw_deadline_t d;
    int32_t ok = 0;

    espw_deadline_start(&d, ms);
    tiku_atomic_enter();
    for (;;) {
        if (q->count < q->len) {
            queue_put(q, item, front);
            ok = 1;
            break;
        }
        if (!espw_deadline_wait(&q->tx_wq, &d, ms)) {
            break;
        }
    }
    tiku_atomic_exit();
    return ok;
}

static int32_t osi_queue_send(void *h, void *item, uint32_t ms) {
    return queue_send(h, item, ms, 0);
}

static int32_t osi_queue_send_to_back(void *h, void *item, uint32_t ms) {
    return queue_send(h, item, ms, 0);
}

static int32_t osi_queue_send_to_front(void *h, void *item, uint32_t ms) {
    return queue_send(h, item, ms, 1);
}

static int32_t osi_queue_send_from_isr(void *h, void *item, void *hptw) {
    osi_queue_t *q = h;
    int32_t ok = 0, woke = 0;

    tiku_atomic_enter();
    if (q->count < q->len) {
        woke = q->rx_wq.waiters != 0U;
        queue_put(q, item, 0);
        ok = 1;
    }
    tiku_atomic_exit();
    if (hptw != NULL && woke) {
        *(int32_t *)hptw = 1;
    }
    return ok;
}

static int32_t osi_queue_recv(void *h, void *item, uint32_t ms) {
    osi_queue_t *q = h;
    espw_deadline_t d;
    int32_t ok = 0;

    espw_deadline_start(&d, ms);
    tiku_atomic_enter();
    for (;;) {
        if (q->count > 0U) {
            memcpy(item, q->buf + q->head * q->item, q->item);
            q->head = (q->head + 1U) % q->len;
            q->count--;
            tiku_thread_wake_one(&q->tx_wq);
            ok = 1;
            break;
        }
        if (!espw_deadline_wait(&q->rx_wq, &d, ms)) {
            break;
        }
    }
    tiku_atomic_exit();
    return ok;
}

static uint32_t osi_queue_msg_waiting(void *h) {
    return ((osi_queue_t *)h)->count;
}

static void *osi_wifi_create_queue(int len, int item) {
    osi_static_queue_t *sq = espw_calloc(1U, sizeof *sq);

    if (sq != NULL) {
        sq->handle = osi_queue_create((uint32_t)len, (uint32_t)item);
        if (sq->handle == NULL) {
            espw_free(sq);
            sq = NULL;
        }
    }
    return sq;
}

static void osi_wifi_delete_queue(void *h) {
    osi_static_queue_t *sq = h;

    if (sq != NULL) {
        osi_queue_delete(sq->handle);
        espw_free(sq);
    }
}

typedef struct {
    volatile uint32_t bits;
    tiku_waitq_t      wq;
} osi_events_t;

static void *osi_event_group_create(void) {
    return espw_obj_alloc(sizeof(osi_events_t));
}

static void osi_event_group_delete(void *h) {
    espw_obj_free(h);
}

static uint32_t osi_event_group_set_bits(void *h, uint32_t bits) {
    osi_events_t *e = h;
    uint32_t now;

    tiku_atomic_enter();
    e->bits |= bits;
    tiku_thread_wake_all(&e->wq);
    now = e->bits;
    tiku_atomic_exit();
    return now;
}

static uint32_t osi_event_group_clear_bits(void *h, uint32_t bits) {
    osi_events_t *e = h;
    uint32_t was;

    tiku_atomic_enter();
    was = e->bits;
    e->bits &= ~bits;
    tiku_atomic_exit();
    return was;
}

static uint32_t osi_event_group_wait_bits(void *h, uint32_t want, int clear,
                                          int all, uint32_t ms) {
    osi_events_t *e = h;
    espw_deadline_t d;
    uint32_t seen;

    espw_deadline_start(&d, ms);
    tiku_atomic_enter();
    for (;;) {
        seen = e->bits;
        if (all ? (seen & want) == want : (seen & want) != 0U) {
            if (clear) {
                e->bits &= ~want;
            }
            break;
        }
        if (!espw_deadline_wait(&e->wq, &d, ms)) {
            seen = e->bits;
            break;
        }
    }
    tiku_atomic_exit();
    return seen;
}

/*---------------------------------------------------------------------------*/
/* Tasks                                                                     */
/*---------------------------------------------------------------------------*/

typedef struct {
    tiku_thread_t          thread;
    void                 (*fn)(void *);
    void                  *arg;
    tiku_waitq_t *volatile wq;          /* what it blocks on, if anything */
    volatile uint8_t       killed;      /* deleted by another: ends at its
                                           next wait */
} osi_task_t;

/* Every task made here, so the radio going down can see each one finish
 * and leave the kernel's table before its memory goes back. */
static osi_task_t *osi_tasks[TIKU_THREADS_MAX];

/** @brief Free the tasks that have ended: out of the table, then memory. */
static void osi_task_reap(void) {
    for (unsigned i = 0U; i < TIKU_THREADS_MAX; i++) {
        osi_task_t *t = osi_tasks[i];

        if (t != NULL && tiku_thread_forget(&t->thread) == 0) {
            osi_tasks[i] = NULL;
            espw_free(t->thread.stack_base);
            espw_free(t);
        }
    }
}

/** @brief The running worker's task, if it is one made here. */
static osi_task_t *osi_task_self(void) {
    tiku_thread_t *self = tiku_thread_self();

    for (unsigned i = 0U; self != NULL && i < TIKU_THREADS_MAX; i++) {
        if (osi_tasks[i] != NULL && &osi_tasks[i]->thread == self) {
            return osi_tasks[i];
        }
    }
    return NULL;
}

/**
 * @brief Note what the running task blocks on (NULL once woken).  A task
 *        another deleted ends here, before or after its wait, leaving the
 *        one atomic section every wait is made in.
 */
static void osi_wait_mark(tiku_waitq_t *q) {
    osi_task_t *me = osi_task_self();

    if (me == NULL) {
        return;
    }
    if (me->killed) {
        tiku_atomic_exit();
        tiku_thread_exit();
    }
    me->wq = q;
}

static void osi_task_entry(void *arg) {
    osi_task_t *t = arg;

    t->fn(t->arg);
}

static int32_t osi_task_create(void *fn, const char *name, uint32_t depth,
                               void *param, uint32_t prio, void *handle) {
    osi_task_t *t;
    uint32_t bytes = ((depth + OSI_TASK_MARGIN) + 7U) & ~7UL;
    uint32_t *stack;

    (void)prio;
    osi_task_reap();
    t = espw_calloc(1U, sizeof *t);
    stack = espw_malloc(bytes);
    if (t == NULL || stack == NULL) {
        espw_free(t);
        espw_free(stack);
        ESPW_PRINTF("task %s: no memory for %lu bytes of stack\n", name,
                    (unsigned long)bytes);
        return 0;
    }
    TRACE("task %s: %lu bytes of stack, priority %lu\n", name,
          (unsigned long)bytes, (unsigned long)prio);
    t->fn = (void (*)(void *))fn;
    t->arg = param;
    t->thread.stack_base = stack;
    t->thread.stack_size = bytes;
    t->thread.state = TIKU_THREAD_UNUSED;
    t->thread.name = name;
    for (unsigned i = 0U; i < TIKU_THREADS_MAX; i++) {
        if (osi_tasks[i] == NULL) {
            osi_tasks[i] = t;
            break;
        }
    }
    if (tiku_thread_start(&t->thread, osi_task_entry, t) != 0) {
        ESPW_PRINTF("task %s: no thread slot\n", name);
        t->thread.state = TIKU_THREAD_DONE;
        osi_task_reap();
        return 0;
    }
    if (handle != NULL) {
        *(void **)handle = t;
    }
    return 1;
}

static int32_t osi_task_create_pinned(void *fn, const char *name,
                                      uint32_t depth, void *param,
                                      uint32_t prio, void *handle,
                                      uint32_t core) {
    (void)core;
    return osi_task_create(fn, name, depth, param, prio, handle);
}

static void osi_task_delete(void *handle) {
    tiku_thread_t *self = tiku_thread_self();

    TRACE("task %s ends\n", handle != NULL ? ((osi_task_t *)handle)->thread.name
                                          : who());
    if (handle == NULL || (self != NULL && handle == (void *)self)) {
        if (self != NULL) {
            tiku_thread_exit();         /* freed later, by osi_task_reap() */
        }
        return;
    }
    /* Another task: it cannot be stopped where it stands, so it ends at its
     * next wait -- woken now if it is in one. */
    tiku_atomic_enter();
    for (unsigned i = 0U; i < TIKU_THREADS_MAX; i++) {
        osi_task_t *t = osi_tasks[i];

        if (t == handle) {
            t->killed = 1U;
            if (t->wq != NULL) {
                tiku_thread_wake_all(t->wq);
            }
        }
    }
    tiku_atomic_exit();
}

void espw_delay_ms(uint32_t ms) {
    tiku_waitq_t nobody = { 0U };
    espw_deadline_t d;

    if (ms == 0U) {
        if (tiku_thread_self() != NULL) {
            tiku_thread_yield();
        }
        return;
    }
    espw_deadline_start(&d, ms);
    tiku_atomic_enter();
    while (espw_deadline_wait(&nobody, &d, ms)) {
    }
    tiku_atomic_exit();
}

static int32_t osi_task_ms_to_tick(uint32_t ms) {
    return (int32_t)ms;
}

static void *osi_task_get_current_task(void) {
    return espw_self();
}

static int32_t osi_task_get_max_priority(void) {
    return OSI_MAX_PRIORITY;
}

static bool osi_is_from_isr(void) {
    return espw_arch_in_isr() != 0;
}

/* A wake from a handler has pended the switch already. */
static void osi_task_yield_from_isr(void) {
}

/*---------------------------------------------------------------------------*/
/* Interrupts                                                                */
/*---------------------------------------------------------------------------*/

/** @brief One of the libraries' lines and the radio line it rides on. */
typedef struct {
    int16_t   lib;          /* the line number the libraries use; -1 free */
    void    (*fn)(void *);
    void     *arg;
} osi_irq_t;

static osi_irq_t osi_irq[OSI_LIB_LINES];
static volatile uint32_t osi_irq_count;

#define OSI_TRAMPOLINE(i)                                                    \
    static void osi_irq_##i(void) {                                          \
        osi_irq_count++;                                                     \
        if (osi_irq[i].fn != NULL) {                                         \
            osi_irq[i].fn(osi_irq[i].arg);                                   \
        }                                                                    \
    }
OSI_TRAMPOLINE(0)
OSI_TRAMPOLINE(1)
OSI_TRAMPOLINE(2)

static const espw_arch_isr_t osi_irq_entry[OSI_LIB_LINES] = {
    osi_irq_0, osi_irq_1, osi_irq_2
};

/** @brief The slot for the libraries' line @p n, taking a free one if new. */
static int osi_irq_slot(uint32_t n, int take) {
    int free_slot = -1;

    for (unsigned i = 0U; i < OSI_LIB_LINES; i++) {
        if (osi_irq[i].lib == (int16_t)n) {
            return (int)i;
        }
        if (osi_irq[i].lib < 0 && free_slot < 0) {
            free_slot = (int)i;
        }
    }
    if (take && free_slot >= 0) {
        osi_irq[free_slot].lib = (int16_t)n;
    }
    return take ? free_slot : -1;
}

static unsigned osi_irq_line(int slot) {
    return ESPW_ARCH_LINE_RADIO + 1U + (unsigned)slot;
}

static void osi_set_intr(int32_t cpu, uint32_t src, uint32_t n, int32_t prio) {
    int i = osi_irq_slot(n, 1);

    (void)cpu;
    (void)prio;
    if (i < 0) {
        ESPW_PRINTF("interrupt %lu: no radio line left\n", (unsigned long)n);
        return;
    }
    TRACE("interrupt %lu: source %lu on line %u\n", (unsigned long)n,
          (unsigned long)src, osi_irq_line(i));
    espw_arch_irq_attach(osi_irq_line(i), src, ESPW_ARCH_LEVEL_DEFAULT,
                             osi_irq_entry[i]);
    espw_arch_irq_mark_flash(osi_irq_line(i), 1);
}

static void osi_clear_intr(uint32_t src, uint32_t n) {
    (void)src;
    (void)n;
}

static void osi_set_isr(int32_t n, void *f, void *arg) {
    int i = osi_irq_slot((uint32_t)n, 1);

    if (i >= 0) {
        uint32_t m = espw_arch_mie_off();

        osi_irq[i].fn = (void (*)(void *))f;
        osi_irq[i].arg = arg;
        espw_arch_mie_restore(m);
    }
}

static void osi_ints_on(uint32_t mask) {
    TRACE("interrupts on: 0x%08lx\n", (unsigned long)mask);
    for (uint32_t n = 0U; n < 32U; n++) {
        int i = (mask & (1UL << n)) ? osi_irq_slot(n, 0) : -1;

        if (i >= 0) {
            espw_arch_irq_enable(osi_irq_line(i));
        }
    }
}

static void osi_ints_off(uint32_t mask) {
    for (uint32_t n = 0U; n < 32U; n++) {
        int i = (mask & (1UL << n)) ? osi_irq_slot(n, 0) : -1;

        if (i >= 0) {
            espw_arch_irq_disable(osi_irq_line(i));
        }
    }
}

static void *osi_spin_lock_create(void) {
    return espw_obj_alloc(sizeof(uint32_t));
}

static void osi_spin_lock_delete(void *lock) {
    espw_obj_free(lock);
}

static uint32_t osi_wifi_int_disable(void *mux) {
    (void)mux;
    return espw_arch_mie_off();
}

static void osi_wifi_int_restore(void *mux, uint32_t tmp) {
    (void)mux;
    espw_arch_mie_restore(tmp);
}

/*---------------------------------------------------------------------------*/
/* Timers                                                                    */
/*---------------------------------------------------------------------------*/

/* The adapter provides the five-word ETSTimer layout used by the libraries. */
typedef struct osi_timer {
    struct osi_timer *next;
    uint32_t          expire;       /* microseconds, wrapping */
    uint32_t          period;       /* 0: once */
    void            (*fn)(void *);
    void             *arg;
} osi_timer_t;

static osi_timer_t *osi_timers;         /* armed, soonest first */
static tiku_waitq_t osi_timer_wq;
static volatile uint8_t osi_running;

uint32_t espw_irq_count(void) {
    return osi_irq_count;
}

int64_t espw_time_us(void) {
    return (int64_t)(espw_arch_systimer() / COUNTS_PER_US);
}

static uint32_t now_us(void) {
    return (uint32_t)espw_time_us();
}

/** @brief The alarm for the soonest timer, or none; interrupts masked. */
static void timer_rearm(void) {
    uint64_t now;
    int32_t ahead;

    if (osi_timers == NULL) {
        espw_arch_alarm_disarm(ESPW_ARCH_ALARM_DRIVER);
        return;
    }
    now = espw_arch_systimer();
    ahead = (int32_t)(osi_timers->expire - (uint32_t)(now / COUNTS_PER_US));
    espw_arch_alarm_arm(ESPW_ARCH_ALARM_DRIVER,
                            now + (ahead > 0 ? (uint64_t)ahead * COUNTS_PER_US
                                             : 0U));
}

/** @brief Take @p t off the armed list; interrupts masked. */
static void timer_unlink(osi_timer_t *t) {
    for (osi_timer_t **l = &osi_timers; *l != NULL; l = &(*l)->next) {
        if (*l == t) {
            *l = t->next;
            break;
        }
    }
    t->next = NULL;
}

/** @brief Put @p t in its place by expiry; interrupts masked. */
static void timer_insert(osi_timer_t *t) {
    osi_timer_t **l = &osi_timers;

    while (*l != NULL && (int32_t)((*l)->expire - t->expire) <= 0) {
        l = &(*l)->next;
    }
    t->next = *l;
    *l = t;
}

void espw_timer_setfn(void *ptimer, void *fn, void *arg) {
    osi_timer_t *t = ptimer;
    uint32_t m = espw_arch_mie_off();

    timer_unlink(t);
    timer_rearm();
    t->fn = (void (*)(void *))fn;
    t->arg = arg;
    t->period = 0U;
    espw_arch_mie_restore(m);
}

void espw_timer_arm_us(void *ptimer, uint32_t us, bool repeat) {
    osi_timer_t *t = ptimer;
    uint32_t m = espw_arch_mie_off();

    timer_unlink(t);
    t->expire = now_us() + us;
    t->period = repeat ? (us != 0U ? us : 1U) : 0U;
    timer_insert(t);
    timer_rearm();
    espw_arch_mie_restore(m);
}

void espw_timer_disarm(void *ptimer) {
    uint32_t m = espw_arch_mie_off();

    timer_unlink((osi_timer_t *)ptimer);
    timer_rearm();
    espw_arch_mie_restore(m);
}

static void osi_timer_arm(void *ptimer, uint32_t ms, bool repeat) {
    espw_timer_arm_us(ptimer, ms * 1000U, repeat);
}

static void osi_timer_done(void *ptimer) {
    espw_timer_disarm(ptimer);
    ((osi_timer_t *)ptimer)->fn = NULL;
}

/** @brief The driver alarm: whatever is due runs in the timer thread. */
static void osi_timer_isr(void) {
    espw_arch_alarm_disarm(ESPW_ARCH_ALARM_DRIVER);
    tiku_thread_wake_one(&osi_timer_wq);
}

/** @brief The timer thread: one due timer at a time, its callback unlocked. */
static void osi_timer_body(void *arg) {
    (void)arg;
    while (osi_running) {
        osi_timer_t *t = NULL;
        void (*fn)(void *) = NULL;
        void *fn_arg = NULL;

        tiku_atomic_enter();
        if (osi_timers != NULL &&
            (int32_t)(osi_timers->expire - now_us()) <= 0) {
            t = osi_timers;
            osi_timers = t->next;
            t->next = NULL;
            fn = t->fn;
            fn_arg = t->arg;
            if (t->period != 0U) {
                t->expire += t->period;
                if ((int32_t)(t->expire - now_us()) < 0) {
                    t->expire = now_us() + t->period;   /* fell behind */
                }
                timer_insert(t);
            }
        }
        timer_rearm();
        /* espw_osi_stop() clears the flag and wakes once; testing it here,
         * inside the atomic section, keeps that wake from going unheard. */
        if (t == NULL && osi_running) {
            (void)tiku_thread_wait(&osi_timer_wq, 0UL);
        }
        tiku_atomic_exit();
        if (fn != NULL) {
            fn(fn_arg);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Memory, time, randomness, the MAC, logging                                */
/*---------------------------------------------------------------------------*/

static void *osi_malloc(size_t n) {
    return espw_malloc(n);
}

/* Packet buffers: PSRAM first when the core has some, SRAM after. */
static void *osi_zalloc_ext(size_t n) {
    return espw_calloc_ext(1U, n);
}

static void *osi_zalloc(size_t n) {
    return espw_calloc(1U, n);
}

static uint32_t osi_free_heap(void) {
    espw_heap_stats_t st;

    espw_heap_stats(&st);
    return st.free;
}

static uint32_t osi_rand(void) {
    uint32_t v = 0U;

#if defined(PLATFORM_ESP32C5)
    if (espw_c5_random((uint8_t *)&v, sizeof(v)) != 0) { abort(); }
#else
    (void)tiku_trng_arch_read_u32(&v);
#endif
    return v;
}

static unsigned long osi_random(void) {
    return osi_rand();
}

static int osi_get_random(uint8_t *buf, size_t len) {
#if defined(PLATFORM_ESP32C5)
    return espw_c5_random(buf, len);
#else
    return tiku_trng_arch_read_bytes(buf, len) == 0 ? 0 : -1;
#endif
}

/* IDF's struct os_time: seconds as a 64-bit time_t, then microseconds. */
static int osi_get_time(void *t) {
    int64_t us = espw_time_us();
    struct {
        int64_t sec;
        int32_t usec;
    } *ot = t;

    ot->sec = us / 1000000;
    ot->usec = (int32_t)(us % 1000000);
    return 0;
}

static uint32_t osi_log_timestamp(void) {
    return (uint32_t)(espw_time_us() / 1000);
}

/* The slow clock's period in microseconds, 19 fraction bits: RC_SLOW. */
static uint32_t osi_slowclk_cal_get(void) {
    return (uint32_t)((1000000ULL << 19) / 136000U);
}

/* Their levels: 1 error, 2 warning, 3 info, 4 debug, 5 verbose. */
#define OSI_LOG_LEVEL   (ESPW_TRACE ? 5U : 3U)

static void osi_log_writev(unsigned int level, const char *tag,
                           const char *fmt, va_list ap) {
    char line[128];

    (void)tag;
    if (level > OSI_LOG_LEVEL) {
        return;
    }
    (void)vsnprintf(line, sizeof line, fmt, ap);
    TIKU_PRINTF("%s", line);
}

static void osi_log_write(unsigned int level, const char *tag,
                          const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    osi_log_writev(level, tag, fmt, ap);
    va_end(ap);
}

/* The Wi-Fi driver takes the stack's events; a build without it has none
 * to take (BLE alone uses this table only for its interrupts and tasks). */
__attribute__((weak)) void espw_event(const char *base, int32_t id,
                                      const void *data, size_t len) {
    (void)base;
    (void)id;
    (void)data;
    (void)len;
}

static int32_t osi_event_post(const char *base, int32_t id, void *data,
                              size_t len, uint32_t ms) {
    (void)ms;
    espw_event(base, id, data, len);
    return ESP_OK;
}

/*---------------------------------------------------------------------------*/
/* What is not used here: NVS, coexistence, retention, power locks           */
/*---------------------------------------------------------------------------*/

/* The libraries' power locks: no light sleep while one is held. */
static void osi_sleep_lock_take(void) {
    espw_arch_sleep_hold(1);
}

static void osi_sleep_lock_give(void) {
    espw_arch_sleep_hold(0);
}

static bool osi_true(void) {
    return true;
}

static bool osi_false(void) {
    return false;
}

static void osi_nothing(void) {
}

#if !ESPW_COEX
static int osi_zero(void) {
    return 0;
}
#endif

static int32_t osi_one(void) {
    return 1;
}

static int osi_phy_country(const char *country) {
    (void)country;
    return ESP_OK;
}

/* NVS: never opened, so nothing is ever read from or written to it. */
static int osi_nvs_open(const char *name, unsigned int mode, uint32_t *h) {
    (void)name;
    (void)mode;
    (void)h;
    return ESP_FAIL;
}

static int osi_nvs_set_i8(uint32_t h, const char *k, int8_t v) {
    (void)h; (void)k; (void)v;
    return ESP_FAIL;
}

static int osi_nvs_get_i8(uint32_t h, const char *k, int8_t *v) {
    (void)h; (void)k; (void)v;
    return ESP_ERR_NVS_NOT_FOUND;
}

static int osi_nvs_set_u8(uint32_t h, const char *k, uint8_t v) {
    (void)h; (void)k; (void)v;
    return ESP_FAIL;
}

static int osi_nvs_get_u8(uint32_t h, const char *k, uint8_t *v) {
    (void)h; (void)k; (void)v;
    return ESP_ERR_NVS_NOT_FOUND;
}

static int osi_nvs_set_u16(uint32_t h, const char *k, uint16_t v) {
    (void)h; (void)k; (void)v;
    return ESP_FAIL;
}

static int osi_nvs_get_u16(uint32_t h, const char *k, uint16_t *v) {
    (void)h; (void)k; (void)v;
    return ESP_ERR_NVS_NOT_FOUND;
}

static void osi_nvs_close(uint32_t h) {
    (void)h;
}

static int osi_nvs_commit(uint32_t h) {
    (void)h;
    return ESP_FAIL;
}

static int osi_nvs_set_blob(uint32_t h, const char *k, const void *v,
                            size_t n) {
    (void)h; (void)k; (void)v; (void)n;
    return ESP_FAIL;
}

static int osi_nvs_get_blob(uint32_t h, const char *k, void *v, size_t *n) {
    (void)h; (void)k; (void)v; (void)n;
    return ESP_ERR_NVS_NOT_FOUND;
}

static int osi_nvs_erase_key(uint32_t h, const char *k) {
    (void)h; (void)k;
    return ESP_FAIL;
}

#if ESPW_COEX
/* Coexistence: both radios built, so the arbiter answers (esp_coex.c). */
static int osi_coex_init(void) {
    return (int)coex_init();
}

static int osi_coex_enable(void) {
    return (int)coex_enable();
}

static uint32_t osi_coex_status(void) {
    return coex_status_get(ESPW_COEX_ST_WIFI);
}

static int osi_coex_register_cb(int type, int (*cb)(int)) {
    return coex_schm_register_callback(type, (void *)cb);
}

#define OSI_COEX(stub, arbiter) arbiter
#else
/* Coexistence: Wi-Fi alone, so every request is granted at once. */
static int osi_coex_request(uint32_t event, uint32_t latency,
                            uint32_t duration) {
    (void)event; (void)latency; (void)duration;
    return 0;
}

static int osi_coex_release(uint32_t event) {
    (void)event;
    return 0;
}

static int osi_coex_channel_set(uint8_t primary, uint8_t secondary) {
    (void)primary; (void)secondary;
    return 0;
}

static int osi_coex_duration_get(uint32_t event, uint32_t *duration) {
    (void)event; (void)duration;
    return 0;
}

static int osi_coex_pti_get(uint32_t event, uint8_t *pti) {
    (void)event; (void)pti;
    return 0;
}

static void osi_coex_status_bits(uint32_t type, uint32_t status) {
    (void)type; (void)status;
}

static uint32_t osi_coex_u32(void) {
    return 0U;
}

static uint8_t osi_coex_u8(void) {
    return 0U;
}

static int osi_coex_interval_set(uint32_t interval) {
    (void)interval;
    return 0;
}

static int osi_coex_register_cb(int type, int (*cb)(int)) {
    (void)type; (void)cb;
    return 0;
}

static int osi_coex_start_cb(int (*cb)(void)) {
    (void)cb;
    return 0;
}

#define OSI_COEX(stub, arbiter) stub
#endif

static int osi_coex_period_set(uint8_t period) {
    (void)period;
    return 0;
}

static uint8_t osi_coex_period_get(void) {
    return 1U;
}

#if !ESPW_COEX
static void *osi_coex_phase(int idx) {
    (void)idx;
    return NULL;
}

static void *osi_null(void) {
    return NULL;
}
#endif

/* Sleep retention: the modem is never powered down under the radio. */
static void osi_regdma_set(void *link, uint32_t v, uint32_t mask) {
    (void)link; (void)v; (void)mask;
}

static void *osi_retention_link(int id) {
    (void)id;
    return NULL;
}

wifi_osi_funcs_t espw_osi_funcs = {
    ._version = ESP_WIFI_OS_ADAPTER_VERSION,
    ._env_is_chip = osi_true,
    ._set_intr = osi_set_intr,
    ._clear_intr = osi_clear_intr,
    ._set_isr = osi_set_isr,
    ._ints_on = osi_ints_on,
    ._ints_off = osi_ints_off,
    ._is_from_isr = osi_is_from_isr,
    ._spin_lock_create = osi_spin_lock_create,
    ._spin_lock_delete = osi_spin_lock_delete,
    ._wifi_int_disable = osi_wifi_int_disable,
    ._wifi_int_restore = osi_wifi_int_restore,
    ._task_yield_from_isr = osi_task_yield_from_isr,
    ._semphr_create = osi_semphr_create,
    ._semphr_delete = osi_semphr_delete,
    ._semphr_take = osi_semphr_take,
    ._semphr_give = osi_semphr_give,
    ._wifi_thread_semphr_get = osi_thread_semphr_get,
    ._mutex_create = osi_mutex_create,
    ._recursive_mutex_create = osi_mutex_create,
    ._mutex_delete = osi_mutex_delete,
    ._mutex_lock = osi_mutex_lock,
    ._mutex_unlock = osi_mutex_unlock,
    ._queue_create = osi_queue_create,
    ._queue_delete = osi_queue_delete,
    ._queue_send = osi_queue_send,
    ._queue_send_from_isr = osi_queue_send_from_isr,
    ._queue_send_to_back = osi_queue_send_to_back,
    ._queue_send_to_front = osi_queue_send_to_front,
    ._queue_recv = osi_queue_recv,
    ._queue_msg_waiting = osi_queue_msg_waiting,
    ._event_group_create = osi_event_group_create,
    ._event_group_delete = osi_event_group_delete,
    ._event_group_set_bits = osi_event_group_set_bits,
    ._event_group_clear_bits = osi_event_group_clear_bits,
    ._event_group_wait_bits = osi_event_group_wait_bits,
    ._task_create_pinned_to_core = osi_task_create_pinned,
    ._task_create = osi_task_create,
    ._task_delete = osi_task_delete,
    ._task_delay = espw_delay_ms,
    ._task_ms_to_tick = osi_task_ms_to_tick,
    ._task_get_current_task = osi_task_get_current_task,
    ._task_get_max_priority = osi_task_get_max_priority,
    ._malloc = osi_malloc,
    ._free = espw_free,
    ._event_post = osi_event_post,
    ._get_free_heap_size = osi_free_heap,
    ._rand = osi_rand,
    ._dport_access_stall_other_cpu_start_wrap = osi_nothing,
    ._dport_access_stall_other_cpu_end_wrap = osi_nothing,
    ._wifi_pm_sleep_lock_acquire = osi_sleep_lock_take,
    ._wifi_pm_sleep_lock_release = osi_sleep_lock_give,
    ._phy_disable = espw_phy_disable,
    ._phy_enable = espw_phy_enable,
    ._phy_update_country_info = osi_phy_country,
    ._read_mac = espw_read_mac,
    ._timer_arm = osi_timer_arm,
    ._timer_disarm = espw_timer_disarm,
    ._timer_done = osi_timer_done,
    ._timer_setfn = espw_timer_setfn,
    ._timer_arm_us = espw_timer_arm_us,
    ._wifi_reset_mac = espw_modem_wifi_reset,
    ._wifi_clock_enable = espw_modem_wifi_clock_on,
    ._wifi_clock_disable = espw_modem_wifi_clock_off,
    ._wifi_rtc_enable_iso = osi_nothing,
    ._wifi_rtc_disable_iso = osi_nothing,
    ._esp_timer_get_time = espw_time_us,
    ._nvs_set_i8 = osi_nvs_set_i8,
    ._nvs_get_i8 = osi_nvs_get_i8,
    ._nvs_set_u8 = osi_nvs_set_u8,
    ._nvs_get_u8 = osi_nvs_get_u8,
    ._nvs_set_u16 = osi_nvs_set_u16,
    ._nvs_get_u16 = osi_nvs_get_u16,
    ._nvs_open = osi_nvs_open,
    ._nvs_close = osi_nvs_close,
    ._nvs_commit = osi_nvs_commit,
    ._nvs_set_blob = osi_nvs_set_blob,
    ._nvs_get_blob = osi_nvs_get_blob,
    ._nvs_erase_key = osi_nvs_erase_key,
    ._get_random = osi_get_random,
    ._get_time = osi_get_time,
    ._random = osi_random,
    ._slowclk_cal_get = osi_slowclk_cal_get,
    ._log_write = osi_log_write,
    ._log_writev = osi_log_writev,
    ._log_timestamp = osi_log_timestamp,
    ._malloc_internal = osi_malloc,
    ._realloc_internal = espw_realloc,
    ._calloc_internal = espw_calloc,
    ._zalloc_internal = osi_zalloc,
    ._wifi_malloc = espw_malloc_ext,
    ._wifi_realloc = espw_realloc_ext,
    ._wifi_calloc = espw_calloc_ext,
    ._wifi_zalloc = osi_zalloc_ext,
    ._wifi_create_queue = osi_wifi_create_queue,
    ._wifi_delete_queue = osi_wifi_delete_queue,
    ._coex_init = OSI_COEX(osi_zero, osi_coex_init),
    ._coex_deinit = OSI_COEX(osi_nothing, coex_deinit),
    ._coex_enable = OSI_COEX(osi_zero, osi_coex_enable),
    ._coex_disable = OSI_COEX(osi_nothing, coex_disable),
    ._coex_status_get = OSI_COEX(osi_coex_u32, osi_coex_status),
    ._coex_condition_set = NULL,
    ._coex_wifi_request = OSI_COEX(osi_coex_request, coex_wifi_request),
    ._coex_wifi_release = OSI_COEX(osi_coex_release, coex_wifi_release),
    ._coex_wifi_channel_set = OSI_COEX(osi_coex_channel_set,
                                       coex_wifi_channel_set),
    ._coex_event_duration_get = OSI_COEX(osi_coex_duration_get,
                                         coex_event_duration_get),
    ._coex_pti_get = OSI_COEX(osi_coex_pti_get, coex_pti_get),
    ._coex_schm_status_bit_clear = OSI_COEX(osi_coex_status_bits,
                                            coex_schm_status_bit_clear),
    ._coex_schm_status_bit_set = OSI_COEX(osi_coex_status_bits,
                                          coex_schm_status_bit_set),
    ._coex_schm_interval_set = OSI_COEX(osi_coex_interval_set,
                                        coex_schm_interval_set),
    ._coex_schm_interval_get = OSI_COEX(osi_coex_u32,
                                        coex_schm_interval_get),
    ._coex_schm_curr_period_get = OSI_COEX(osi_coex_u8,
                                           coex_schm_curr_period_get),
    ._coex_schm_curr_phase_get = OSI_COEX(osi_null,
                                          coex_schm_curr_phase_get),
    ._coex_schm_process_restart = OSI_COEX(osi_zero,
                                           coex_schm_process_restart),
    ._coex_schm_register_cb = osi_coex_register_cb,
    ._coex_register_start_cb = OSI_COEX(osi_coex_start_cb,
                                        coex_register_start_cb),
    ._regdma_link_set_write_wait_content = osi_regdma_set,
    ._sleep_retention_find_link_by_id = osi_retention_link,
    ._coex_schm_flexible_period_set = osi_coex_period_set,
    ._coex_schm_flexible_period_get = osi_coex_period_get,
    ._coex_schm_get_phase_by_idx = OSI_COEX(osi_coex_phase,
                                            coex_schm_get_phase_by_idx),
    ._wifi_disable_ac_ax = osi_false,
    ._wifi_bb_sleep_retention_attach = osi_one,
    ._wifi_bb_sleep_retention_detach = osi_one,
    ._wifi_mac_sleep_retention_attach = osi_one,
    ._wifi_mac_sleep_retention_detach = osi_one,
    ._magic = (int32_t)ESP_WIFI_OS_ADAPTER_MAGIC,
};

/*---------------------------------------------------------------------------*/
/* Start and stop                                                            */
/*---------------------------------------------------------------------------*/

int espw_osi_start(void) {
    for (unsigned i = 0U; i < OSI_LIB_LINES; i++) {
        osi_irq[i].lib = -1;
        osi_irq[i].fn = NULL;
    }
    memset(osi_tasks, 0, sizeof osi_tasks);
    osi_timers = NULL;
    osi_timer_wq.waiters = 0U;

    espw_arch_irq_attach(ESPW_ARCH_LINE_RADIO,
                             ESPW_ARCH_SRC_SYSTIMER(ESPW_ARCH_ALARM_DRIVER),
                             ESPW_ARCH_LEVEL_TIMER, osi_timer_isr);
    espw_arch_alarm_disarm(ESPW_ARCH_ALARM_DRIVER);
    espw_arch_irq_enable(ESPW_ARCH_LINE_RADIO);

    osi_running = 1U;
    if (osi_task_create((void *)osi_timer_body, "espw-timer",
                        OSI_TIMER_STACK - OSI_TASK_MARGIN, NULL, 0U, NULL) != 1) {
        osi_running = 0U;
        return -1;
    }
    return 0;
}

int espw_osi_stop(void) {
    uint32_t m = espw_arch_mie_off();
    int left = 0;

    osi_running = 0U;
    osi_timers = NULL;
    espw_arch_alarm_disarm(ESPW_ARCH_ALARM_DRIVER);
    espw_arch_mie_restore(m);
    for (unsigned i = 0U; i < ESPW_ARCH_LINES_RADIO; i++) {
        espw_arch_irq_disable(ESPW_ARCH_LINE_RADIO + i);
        espw_arch_irq_mark_flash(ESPW_ARCH_LINE_RADIO + i, 0);
    }
    /* The timer thread sees the flag at its next pass and ends; the stack's
     * own tasks have ended with its deinit.  A second at most for both. */
    tiku_thread_wake_one(&osi_timer_wq);
    for (unsigned tries = 0U; tries < 100U; tries++) {
        osi_task_reap();
        left = 0;
        for (unsigned i = 0U; i < TIKU_THREADS_MAX; i++) {
            left += osi_tasks[i] != NULL;
        }
        if (left == 0) {
            break;
        }
        espw_delay_ms(10U);
    }
    for (unsigned i = 0U; i < TIKU_THREADS_MAX; i++) {
        if (osi_tasks[i] != NULL) {
            ESPW_PRINTF("task %s did not end\n", osi_tasks[i]->thread.name);
        }
    }
    return left == 0 ? 0 : -1;
}
