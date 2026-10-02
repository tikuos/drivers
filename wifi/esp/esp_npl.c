/*
 * Tiku Drivers - ESP32-C61 BLE controller, the OS it runs on (its NPL)
 *
 * The controller reaches its OS through one table: events and event
 * queues, mutexes, semaphores, callouts (one-shot timers), time in
 * milliseconds and critical sections.  Each object it hands over is one
 * pointer for the port to fill; here they point into pools sized by what
 * the controller says it will make.  Blocking is on kernel wait queues (its
 * task is a worker thread); callouts run on the shim's timer service.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tiku.h"
#include <hal/tiku_cpu.h>
#include <kernel/threads/tiku_thread.h>
#include <arch/esp32c61/tiku_crt_early.h>
#include <arch/esp32c61/tiku_irq_arch.h>

#include "esp_ble.h"
#include "esp_ble_abi.h"
#include "esp_heap.h"
#include "esp_port.h"

#define NPL_SEM_MAX     128U            /* a semaphore's ceiling, as IDF's */
#define NPL_US_MAX      0x7FFFFFFFUL    /* the timer service's horizon */

/*---------------------------------------------------------------------------*/
/* Objects and their pools                                                   */
/*---------------------------------------------------------------------------*/

typedef struct npl_evq npl_evq_t;

typedef struct npl_ev {
    struct npl_ev        *next;         /* in its queue */
    npl_evq_t            *q;            /* the queue it is in; NULL: none */
    struct ble_npl_event *handle;       /* the controller's, as get returns */
    ble_npl_event_fn     *fn;
    void                 *arg;
} npl_ev_t;

struct npl_evq {
    npl_ev_t     *head;
    npl_ev_t     *tail;
    tiku_waitq_t  wq;
};

typedef struct {
    void         *owner;
    uint32_t      depth;
    tiku_waitq_t  wq;
} npl_mutex_t;

typedef struct {
    uint16_t      count;
    tiku_waitq_t  wq;
} npl_sem_t;

typedef struct {
    uint32_t               timer[5];    /* the shim's timer, opaque */
    struct ble_npl_eventq *evq;         /* where it fires; NULL: runs */
    struct ble_npl_event   ev;
    volatile uint8_t       active;
    uint32_t               expiry;      /* milliseconds */
} npl_co_t;

typedef struct {
    uint8_t  *base;
    uint32_t *used;                     /* a bit per object */
    uint16_t  size;
    uint16_t  count;
    uint16_t  peak;
    uint16_t  live;
} npl_pool_t;

enum { POOL_EV, POOL_EVQ, POOL_CO, POOL_SEM, POOL_MUTEX, POOL_KINDS };

static npl_pool_t npl_pools[POOL_KINDS];
static const char *const npl_kind[POOL_KINDS] = {
    "event", "event queue", "callout", "semaphore", "mutex"
};

static int pool_make(npl_pool_t *p, uint16_t size, uint16_t count) {
    memset(p, 0, sizeof *p);
    p->size = (uint16_t)((size + 3U) & ~3U);
    p->count = count;
    if (count == 0U) {
        return 0;
    }
    p->base = espw_calloc(count, p->size);
    p->used = espw_calloc((count + 31U) / 32U, sizeof(uint32_t));
    if (p->base == NULL || p->used == NULL) {
        espw_free(p->base);
        espw_free(p->used);
        memset(p, 0, sizeof *p);
        return -1;
    }
    return 0;
}

static void pool_free(npl_pool_t *p) {
    espw_free(p->base);
    espw_free(p->used);
    memset(p, 0, sizeof *p);
}

/** @brief Whether @p obj is one of @p p's objects, and given out. */
static int pool_owns(const npl_pool_t *p, const void *obj) {
    uintptr_t a = (uintptr_t)obj, b = (uintptr_t)p->base;
    uint32_t i;

    if (p->base == NULL || a < b || a >= b + (uintptr_t)p->count * p->size ||
        (a - b) % p->size != 0U) {
        return 0;
    }
    i = (uint32_t)((a - b) / p->size);
    return (p->used[i / 32U] & (1UL << (i % 32U))) != 0U;
}

/** @brief A zeroed object, or a fault: the controller cannot go on
 *         without one, and the dump says which kind ran out. */
static void *pool_get(int kind) {
    npl_pool_t *p = &npl_pools[kind];
    uint32_t m = tiku_esp32c61_mie_off();

    for (uint32_t i = 0U; i < p->count; i++) {
        uint32_t bit = 1UL << (i % 32U);

        if ((p->used[i / 32U] & bit) == 0U) {
            void *obj = p->base + i * p->size;

            p->used[i / 32U] |= bit;
            if (++p->live > p->peak) {
                p->peak = p->live;
            }
            tiku_esp32c61_mie_restore(m);
            memset(obj, 0, p->size);
            return obj;
        }
    }
    tiku_esp32c61_mie_restore(m);
    ESPB_PRINTF("out of NPL %ss (%u made)\n", npl_kind[kind],
                (unsigned)p->count);
    __builtin_trap();
}

static void pool_put(int kind, void *obj) {
    npl_pool_t *p = &npl_pools[kind];
    uint32_t m = tiku_esp32c61_mie_off();

    if (pool_owns(p, obj)) {
        uint32_t i = (uint32_t)(((uintptr_t)obj - (uintptr_t)p->base) /
                                p->size);

        p->used[i / 32U] &= ~(1UL << (i % 32U));
        p->live--;
    }
    tiku_esp32c61_mie_restore(m);
}

/*---------------------------------------------------------------------------*/
/* Events and event queues                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Take @p e off the queue it is in; atomic section held. */
static void ev_unlink(npl_ev_t *e) {
    npl_evq_t *q = e->q;
    npl_ev_t *prev = NULL, *cur;

    if (q == NULL) {
        return;
    }
    for (cur = q->head; cur != NULL && cur != e; cur = cur->next) {
        prev = cur;
    }
    if (cur == e) {
        if (prev != NULL) {
            prev->next = e->next;
        } else {
            q->head = e->next;
        }
        if (q->tail == e) {
            q->tail = prev;
        }
    }
    e->next = NULL;
    e->q = NULL;
}

/** @brief Empty @p q, every event in it no longer queued. */
static void evq_clear(npl_evq_t *q) {
    tiku_atomic_enter();
    while (q->head != NULL) {
        npl_ev_t *e = q->head;

        q->head = e->next;
        e->next = NULL;
        e->q = NULL;
    }
    q->tail = NULL;
    tiku_atomic_exit();
}

static void npl_event_init(struct ble_npl_event *ev, ble_npl_event_fn *fn,
                           void *arg) {
    npl_ev_t *e = ev->event;

    if (pool_owns(&npl_pools[POOL_EV], e)) {
        tiku_atomic_enter();
        ev_unlink(e);
        tiku_atomic_exit();
    } else {
        e = pool_get(POOL_EV);
        ev->event = e;
    }
    e->handle = ev;
    e->fn = fn;
    e->arg = arg;
}

static void npl_event_deinit(struct ble_npl_event *ev) {
    npl_ev_t *e = ev->event;

    if (pool_owns(&npl_pools[POOL_EV], e)) {
        tiku_atomic_enter();
        ev_unlink(e);
        tiku_atomic_exit();
        pool_put(POOL_EV, e);
    }
    ev->event = NULL;
}

static void npl_event_reset(struct ble_npl_event *ev) {
    npl_ev_t *e = ev->event;

    if (e != NULL) {
        tiku_atomic_enter();
        ev_unlink(e);
        tiku_atomic_exit();
    }
}

static void npl_event_run(struct ble_npl_event *ev) {
    npl_ev_t *e = ev->event;

    e->fn(ev);
}

static bool npl_event_is_queued(struct ble_npl_event *ev) {
    npl_ev_t *e = ev->event;

    return e != NULL && e->q != NULL;
}

static void *npl_event_get_arg(struct ble_npl_event *ev) {
    return ((npl_ev_t *)ev->event)->arg;
}

static void npl_event_set_arg(struct ble_npl_event *ev, void *arg) {
    ((npl_ev_t *)ev->event)->arg = arg;
}

static void npl_eventq_init(struct ble_npl_eventq *evq) {
    npl_evq_t *q = evq->eventq;

    if (pool_owns(&npl_pools[POOL_EVQ], q)) {
        evq_clear(q);
    } else {
        evq->eventq = pool_get(POOL_EVQ);
    }
}

static void npl_eventq_deinit(struct ble_npl_eventq *evq) {
    npl_evq_t *q = evq->eventq;

    if (pool_owns(&npl_pools[POOL_EVQ], q)) {
        evq_clear(q);
        pool_put(POOL_EVQ, q);
    }
    evq->eventq = NULL;
}

/** @brief Queue @p ev at the back or the front, once: an event already in a
 *         queue stays where it is. */
static void evq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev,
                    int front) {
    npl_evq_t *q = evq->eventq;
    npl_ev_t *e = ev->event;

    if (q == NULL || e == NULL) {
        return;
    }
    tiku_atomic_enter();
    if (e->q == NULL) {
        e->q = q;
        if (front) {
            e->next = q->head;
            q->head = e;
            if (q->tail == NULL) {
                q->tail = e;
            }
        } else {
            e->next = NULL;
            if (q->tail != NULL) {
                q->tail->next = e;
            } else {
                q->head = e;
            }
            q->tail = e;
        }
        tiku_thread_wake_all(&q->wq);
    }
    tiku_atomic_exit();
}

static void npl_eventq_put(struct ble_npl_eventq *evq,
                           struct ble_npl_event *ev) {
    evq_put(evq, ev, 0);
}

static void npl_eventq_put_to_front(struct ble_npl_eventq *evq,
                                    struct ble_npl_event *ev) {
    evq_put(evq, ev, 1);
}

static void npl_eventq_remove(struct ble_npl_eventq *evq,
                              struct ble_npl_event *ev) {
    npl_ev_t *e = ev->event;

    if (e == NULL) {
        return;
    }
    tiku_atomic_enter();
    if (e->q != NULL && e->q == evq->eventq) {
        ev_unlink(e);
    }
    tiku_atomic_exit();
}

static struct ble_npl_event *npl_eventq_get(struct ble_npl_eventq *evq,
                                            ble_npl_time_t tmo) {
    npl_evq_t *q = evq->eventq;
    npl_ev_t *e = NULL;
    espw_deadline_t d;

    espw_deadline_start(&d, tmo);
    tiku_atomic_enter();
    for (;;) {
        e = q->head;
        if (e == NULL && tmo != 0U) {
            espb_controller_idle();
        }
        if (e != NULL) {
            q->head = e->next;
            if (q->head == NULL) {
                q->tail = NULL;
            }
            e->next = NULL;
            e->q = NULL;
            break;
        }
        if (!espw_deadline_wait(&q->wq, &d, tmo)) {
            break;
        }
    }
    tiku_atomic_exit();
    return e != NULL ? e->handle : NULL;
}

static bool npl_eventq_is_empty(struct ble_npl_eventq *evq) {
    return ((npl_evq_t *)evq->eventq)->head == NULL;
}

/*---------------------------------------------------------------------------*/
/* Mutexes and semaphores                                                    */
/*---------------------------------------------------------------------------*/

static ble_npl_error_t npl_mutex_init(struct ble_npl_mutex *mu) {
    if (!pool_owns(&npl_pools[POOL_MUTEX], mu->mutex)) {
        mu->mutex = pool_get(POOL_MUTEX);
    }
    return BLE_NPL_OK;
}

static ble_npl_error_t npl_mutex_deinit(struct ble_npl_mutex *mu) {
    if (!pool_owns(&npl_pools[POOL_MUTEX], mu->mutex)) {
        return BLE_NPL_INVALID_PARAM;
    }
    pool_put(POOL_MUTEX, mu->mutex);
    mu->mutex = NULL;
    return BLE_NPL_OK;
}

static ble_npl_error_t npl_mutex_pend(struct ble_npl_mutex *mu,
                                      ble_npl_time_t tmo) {
    npl_mutex_t *m = mu->mutex;
    void *me = espw_self();
    ble_npl_error_t rc = BLE_NPL_TIMEOUT;
    espw_deadline_t d;

    if (m == NULL) {
        return BLE_NPL_INVALID_PARAM;
    }
    if (tiku_esp32c61_in_isr()) {
        return BLE_NPL_ERR_IN_ISR;
    }
    espw_deadline_start(&d, tmo);
    tiku_atomic_enter();
    for (;;) {
        if (m->depth == 0U || m->owner == me) {
            m->owner = me;
            m->depth++;
            rc = BLE_NPL_OK;
            break;
        }
        if (!espw_deadline_wait(&m->wq, &d, tmo)) {
            break;
        }
    }
    tiku_atomic_exit();
    return rc;
}

static ble_npl_error_t npl_mutex_release(struct ble_npl_mutex *mu) {
    npl_mutex_t *m = mu->mutex;
    ble_npl_error_t rc = BLE_NPL_BAD_MUTEX;

    if (m == NULL) {
        return BLE_NPL_INVALID_PARAM;
    }
    tiku_atomic_enter();
    if (m->depth != 0U && m->owner == espw_self()) {
        if (--m->depth == 0U) {
            m->owner = NULL;
            tiku_thread_wake_one(&m->wq);
        }
        rc = BLE_NPL_OK;
    }
    tiku_atomic_exit();
    return rc;
}

static ble_npl_error_t npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens) {
    if (!pool_owns(&npl_pools[POOL_SEM], sem->sem)) {
        npl_sem_t *s = pool_get(POOL_SEM);

        s->count = tokens;
        sem->sem = s;
    }
    return BLE_NPL_OK;
}

static ble_npl_error_t npl_sem_deinit(struct ble_npl_sem *sem) {
    if (!pool_owns(&npl_pools[POOL_SEM], sem->sem)) {
        return BLE_NPL_INVALID_PARAM;
    }
    pool_put(POOL_SEM, sem->sem);
    sem->sem = NULL;
    return BLE_NPL_OK;
}

static ble_npl_error_t npl_sem_pend(struct ble_npl_sem *sem,
                                    ble_npl_time_t tmo) {
    npl_sem_t *s = sem->sem;
    ble_npl_error_t rc = BLE_NPL_TIMEOUT;
    espw_deadline_t d;

    if (s == NULL) {
        return BLE_NPL_INVALID_PARAM;
    }
    espw_deadline_start(&d, tmo);
    tiku_atomic_enter();
    for (;;) {
        if (s->count != 0U) {
            s->count--;
            rc = BLE_NPL_OK;
            break;
        }
        if (!espw_deadline_wait(&s->wq, &d, tmo)) {
            break;
        }
    }
    tiku_atomic_exit();
    return rc;
}

static ble_npl_error_t npl_sem_release(struct ble_npl_sem *sem) {
    npl_sem_t *s = sem->sem;

    if (s == NULL) {
        return BLE_NPL_INVALID_PARAM;
    }
    tiku_atomic_enter();
    if (s->count < NPL_SEM_MAX) {
        s->count++;
        tiku_thread_wake_one(&s->wq);
    }
    tiku_atomic_exit();
    return BLE_NPL_OK;
}

static uint16_t npl_sem_get_count(struct ble_npl_sem *sem) {
    return ((npl_sem_t *)sem->sem)->count;
}

/*---------------------------------------------------------------------------*/
/* Time and callouts                                                         */
/*---------------------------------------------------------------------------*/

static uint32_t npl_time_get(void) {
    return (uint32_t)(espw_time_us() / 1000);
}

static ble_npl_error_t npl_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *out) {
    *out = ms;
    return BLE_NPL_OK;
}

static ble_npl_error_t npl_time_ticks_to_ms(ble_npl_time_t t, uint32_t *out) {
    *out = t;
    return BLE_NPL_OK;
}

static ble_npl_time_t npl_time_same(uint32_t t) {
    return t;
}

static void npl_time_delay(ble_npl_time_t ms) {
    espw_delay_ms(ms);
}

static uint32_t npl_time_forever(void) {
    return BLE_NPL_TIME_FOREVER;
}

/**
 * @brief A callout's timer ran out (in the timer thread): its event goes to
 *        its queue, or runs here.  A stop or a later reset that came between
 *        the timer service taking it and this call is honoured.
 */
static void npl_co_fire(void *arg) {
    npl_co_t *c = arg;
    struct ble_npl_eventq *evq;
    int due;

    tiku_atomic_enter();
    due = c->active && (int32_t)(npl_time_get() - c->expiry) >= 0;
    if (due) {
        c->active = 0U;
    }
    evq = c->evq;
    tiku_atomic_exit();
    if (!due) {
        return;
    }
    if (evq != NULL) {
        npl_eventq_put(evq, &c->ev);
    } else {
        npl_event_run(&c->ev);
    }
}

static int npl_callout_init(struct ble_npl_callout *co,
                            struct ble_npl_eventq *evq, ble_npl_event_fn *fn,
                            void *arg) {
    npl_co_t *c = co->co;

    if (!pool_owns(&npl_pools[POOL_CO], c)) {
        c = pool_get(POOL_CO);
        espw_timer_setfn(c->timer, (void *)npl_co_fire, c);
        co->co = c;
    }
    c->evq = evq;
    npl_event_init(&c->ev, fn, arg);
    return 0;
}

static void npl_callout_stop(struct ble_npl_callout *co) {
    npl_co_t *c = co->co;

    if (c == NULL) {
        return;
    }
    espw_timer_disarm(c->timer);
    c->active = 0U;
    if (c->evq != NULL) {
        npl_eventq_remove(c->evq, &c->ev);
    }
}

static ble_npl_error_t npl_callout_reset(struct ble_npl_callout *co,
                                         ble_npl_time_t ticks) {
    npl_co_t *c = co->co;
    uint64_t us = (uint64_t)ticks * 1000U;

    npl_callout_stop(co);
    tiku_atomic_enter();
    c->expiry = npl_time_get() + ticks;
    c->active = 1U;
    tiku_atomic_exit();
    espw_timer_arm_us(c->timer, us > NPL_US_MAX ? NPL_US_MAX : (uint32_t)us,
                      false);
    return BLE_NPL_OK;
}

static void npl_callout_deinit(struct ble_npl_callout *co) {
    npl_co_t *c = co->co;

    if (!pool_owns(&npl_pools[POOL_CO], c)) {
        co->co = NULL;
        return;
    }
    npl_callout_stop(co);
    npl_event_deinit(&c->ev);
    pool_put(POOL_CO, c);
    co->co = NULL;
}

static void npl_callout_mem_reset(struct ble_npl_callout *co) {
    npl_event_reset(&((npl_co_t *)co->co)->ev);
}

static bool npl_callout_is_active(struct ble_npl_callout *co) {
    return ((npl_co_t *)co->co)->active != 0U;
}

static ble_npl_time_t npl_callout_get_ticks(struct ble_npl_callout *co) {
    npl_co_t *c = co->co;

    return c->active ? c->expiry : 0U;
}

static uint32_t npl_callout_remaining_ticks(struct ble_npl_callout *co,
                                            ble_npl_time_t now) {
    npl_co_t *c = co->co;
    int32_t left = (int32_t)(c->expiry - now);

    return (c->active && left > 0) ? (uint32_t)left : 0U;
}

static void npl_callout_set_arg(struct ble_npl_callout *co, void *arg) {
    npl_event_set_arg(&((npl_co_t *)co->co)->ev, arg);
}

/*---------------------------------------------------------------------------*/
/* Critical sections, the scheduler                                          */
/*---------------------------------------------------------------------------*/

static uint32_t         npl_crit_mie;
static volatile uint8_t npl_crit_depth;

static uint32_t npl_hw_enter_critical(void) {
    uint32_t m = tiku_esp32c61_mie_off();

    if (npl_crit_depth++ == 0U) {
        npl_crit_mie = m;
    }
    return 0U;
}

static void npl_hw_exit_critical(uint32_t ctx) {
    (void)ctx;
    if (npl_crit_depth != 0U && --npl_crit_depth == 0U) {
        tiku_esp32c61_mie_restore(npl_crit_mie);
    }
}

static uint8_t npl_hw_is_in_critical(void) {
    return npl_crit_depth;
}

static bool npl_os_started(void) {
    return true;
}

espb_npl_funcs_t espb_npl_funcs = {
    .p_ble_npl_os_started = npl_os_started,
    .p_ble_npl_get_current_task_id = espw_self,
    .p_ble_npl_eventq_init = npl_eventq_init,
    .p_ble_npl_eventq_deinit = npl_eventq_deinit,
    .p_ble_npl_eventq_get = npl_eventq_get,
    .p_ble_npl_eventq_put = npl_eventq_put,
    .p_ble_npl_eventq_remove = npl_eventq_remove,
    .p_ble_npl_event_run = npl_event_run,
    .p_ble_npl_eventq_is_empty = npl_eventq_is_empty,
    .p_ble_npl_event_init = npl_event_init,
    .p_ble_npl_event_deinit = npl_event_deinit,
    .p_ble_npl_event_reset = npl_event_reset,
    .p_ble_npl_event_is_queued = npl_event_is_queued,
    .p_ble_npl_event_get_arg = npl_event_get_arg,
    .p_ble_npl_event_set_arg = npl_event_set_arg,
    .p_ble_npl_mutex_init = npl_mutex_init,
    .p_ble_npl_mutex_deinit = npl_mutex_deinit,
    .p_ble_npl_mutex_pend = npl_mutex_pend,
    .p_ble_npl_mutex_release = npl_mutex_release,
    .p_ble_npl_sem_init = npl_sem_init,
    .p_ble_npl_sem_deinit = npl_sem_deinit,
    .p_ble_npl_sem_pend = npl_sem_pend,
    .p_ble_npl_sem_release = npl_sem_release,
    .p_ble_npl_sem_get_count = npl_sem_get_count,
    .p_ble_npl_callout_init = npl_callout_init,
    .p_ble_npl_callout_reset = npl_callout_reset,
    .p_ble_npl_callout_stop = npl_callout_stop,
    .p_ble_npl_callout_deinit = npl_callout_deinit,
    .p_ble_npl_callout_mem_reset = npl_callout_mem_reset,
    .p_ble_npl_callout_is_active = npl_callout_is_active,
    .p_ble_npl_callout_get_ticks = npl_callout_get_ticks,
    .p_ble_npl_callout_remaining_ticks = npl_callout_remaining_ticks,
    .p_ble_npl_callout_set_arg = npl_callout_set_arg,
    .p_ble_npl_time_get = npl_time_get,
    .p_ble_npl_time_ms_to_ticks = npl_time_ms_to_ticks,
    .p_ble_npl_time_ticks_to_ms = npl_time_ticks_to_ms,
    .p_ble_npl_time_ms_to_ticks32 = npl_time_same,
    .p_ble_npl_time_ticks_to_ms32 = npl_time_same,
    .p_ble_npl_time_delay = npl_time_delay,
    .p_ble_npl_hw_set_isr = NULL,
    .p_ble_npl_hw_enter_critical = npl_hw_enter_critical,
    .p_ble_npl_hw_exit_critical = npl_hw_exit_critical,
    .p_ble_npl_get_time_forever = npl_time_forever,
    .p_ble_npl_hw_is_in_critical = npl_hw_is_in_critical,
    .p_ble_npl_eventq_put_to_front = npl_eventq_put_to_front,
};

/*---------------------------------------------------------------------------*/
/* Bring-up                                                                  */
/*---------------------------------------------------------------------------*/

int espb_npl_init(const espb_npl_counts_t *n) {
    /* A callout carries an event of its own, beyond the counted ones. */
    uint16_t events = (uint16_t)(n->evt_count + n->co_count);

    npl_crit_depth = 0U;
    if (pool_make(&npl_pools[POOL_EV], sizeof(npl_ev_t), events) != 0 ||
        pool_make(&npl_pools[POOL_EVQ], sizeof(npl_evq_t), n->evtq_count) ||
        pool_make(&npl_pools[POOL_CO], sizeof(npl_co_t), n->co_count) ||
        pool_make(&npl_pools[POOL_SEM], sizeof(npl_sem_t), n->sem_count) ||
        pool_make(&npl_pools[POOL_MUTEX], sizeof(npl_mutex_t),
                  n->mutex_count)) {
        espb_npl_deinit();
        return -1;
    }
    return 0;
}

void espb_npl_deinit(void) {
    npl_pool_t *co = &npl_pools[POOL_CO];

    /* A callout the controller left armed must not fire into freed memory. */
    for (uint32_t i = 0U; i < co->count; i++) {
        npl_co_t *c = (npl_co_t *)(void *)(co->base + i * co->size);

        if (pool_owns(co, c)) {
            espw_timer_disarm(c->timer);
        }
    }
    for (int k = 0; k < POOL_KINDS; k++) {
        if (npl_pools[k].live != 0U) {
            ESPB_PRINTF("%u NPL %ss still live at deinit\n",
                        (unsigned)npl_pools[k].live, npl_kind[k]);
        }
        pool_free(&npl_pools[k]);
    }
}

void espb_npl_stats(uint16_t live[5], uint16_t peak[5], uint16_t made[5]) {
    for (int k = 0; k < POOL_KINDS; k++) {
        live[k] = npl_pools[k].live;
        peak[k] = npl_pools[k].peak;
        made[k] = npl_pools[k].count;
    }
}
