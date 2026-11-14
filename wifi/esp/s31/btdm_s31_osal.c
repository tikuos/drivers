/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * btdm_s31_osal.c - BTDM OS callbacks over TikuOS workers and NPL pools.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "btdm_s31_osal.h"
#include "wifi_s31_arch.h"
#include "../esp_ble.h"
#include "../esp_port.h"
#include "../esp_heap.h"
#include <kernel/threads/tiku_thread.h>
#include <string.h>
void wr_btdm_osal_eventq_init(struct ble_npl_eventq *evq)
{ espb_npl_funcs.p_ble_npl_eventq_init(evq); }
void wr_btdm_osal_eventq_deinit(struct ble_npl_eventq *evq)
{ espb_npl_funcs.p_ble_npl_eventq_deinit(evq); }
struct ble_npl_event *wr_btdm_osal_eventq_get(struct ble_npl_eventq *evq, uint32_t tmo)
{ return espb_npl_funcs.p_ble_npl_eventq_get(evq, tmo); }
void wr_btdm_osal_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{ espb_npl_funcs.p_ble_npl_eventq_put(evq, ev); }
void wr_btdm_osal_eventq_put_to_front(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{ espb_npl_funcs.p_ble_npl_eventq_put_to_front(evq, ev); }
void wr_btdm_osal_eventq_remove(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{ espb_npl_funcs.p_ble_npl_eventq_remove(evq, ev); }
bool wr_btdm_osal_eventq_is_empty(struct ble_npl_eventq *evq)
{ return espb_npl_funcs.p_ble_npl_eventq_is_empty(evq); }
void wr_btdm_osal_event_run(struct ble_npl_event *ev)
{ espb_npl_funcs.p_ble_npl_event_run(ev); }
void wr_btdm_osal_event_init(struct ble_npl_event *ev, ble_npl_event_fn *fn, void *arg)
{ espb_npl_funcs.p_ble_npl_event_init(ev, fn, arg); }
void wr_btdm_osal_event_deinit(struct ble_npl_event *ev)
{ espb_npl_funcs.p_ble_npl_event_deinit(ev); }
void wr_btdm_osal_event_reset(struct ble_npl_event *ev)
{ espb_npl_funcs.p_ble_npl_event_reset(ev); }
bool wr_btdm_osal_event_is_queued(struct ble_npl_event *ev)
{ return espb_npl_funcs.p_ble_npl_event_is_queued(ev); }
void *wr_btdm_osal_event_get_arg(struct ble_npl_event *ev)
{ return espb_npl_funcs.p_ble_npl_event_get_arg(ev); }
void wr_btdm_osal_event_set_arg(struct ble_npl_event *ev, void *arg)
{ espb_npl_funcs.p_ble_npl_event_set_arg(ev, arg); }
int wr_btdm_osal_mutex_init(struct ble_npl_mutex *mu)
{ return espb_npl_funcs.p_ble_npl_mutex_init(mu); }
int wr_btdm_osal_mutex_deinit(struct ble_npl_mutex *mu)
{ return espb_npl_funcs.p_ble_npl_mutex_deinit(mu); }
int wr_btdm_osal_mutex_pend(struct ble_npl_mutex *mu, uint32_t timeout)
{ return espb_npl_funcs.p_ble_npl_mutex_pend(mu, timeout); }
int wr_btdm_osal_mutex_release(struct ble_npl_mutex *mu)
{ return espb_npl_funcs.p_ble_npl_mutex_release(mu); }
int wr_btdm_osal_sem_init(struct ble_npl_sem *sem, uint16_t tokens)
{ return espb_npl_funcs.p_ble_npl_sem_init(sem, tokens); }
int wr_btdm_osal_sem_deinit(struct ble_npl_sem *sem)
{ return espb_npl_funcs.p_ble_npl_sem_deinit(sem); }
int wr_btdm_osal_sem_pend(struct ble_npl_sem *sem, uint32_t timeout)
{ return espb_npl_funcs.p_ble_npl_sem_pend(sem, timeout); }
int wr_btdm_osal_sem_release(struct ble_npl_sem *sem)
{ return espb_npl_funcs.p_ble_npl_sem_release(sem); }
uint16_t wr_btdm_osal_sem_get_count(struct ble_npl_sem *sem)
{ return espb_npl_funcs.p_ble_npl_sem_get_count(sem); }
int wr_btdm_osal_callout_init(struct ble_npl_callout *co, struct ble_npl_eventq *evq, ble_npl_event_fn *ev_cb, void *ev_arg)
{ return espb_npl_funcs.p_ble_npl_callout_init(co, evq, ev_cb, ev_arg); }
void wr_btdm_osal_callout_deinit(struct ble_npl_callout *co)
{ espb_npl_funcs.p_ble_npl_callout_deinit(co); }
int wr_btdm_osal_callout_reset(struct ble_npl_callout *co, uint32_t ticks)
{ return espb_npl_funcs.p_ble_npl_callout_reset(co, ticks); }
void wr_btdm_osal_callout_mem_reset(struct ble_npl_callout *co)
{ espb_npl_funcs.p_ble_npl_callout_mem_reset(co); }
void wr_btdm_osal_callout_stop(struct ble_npl_callout *co)
{ espb_npl_funcs.p_ble_npl_callout_stop(co); }
bool wr_btdm_osal_callout_is_active(struct ble_npl_callout *co)
{ return espb_npl_funcs.p_ble_npl_callout_is_active(co); }
uint32_t wr_btdm_osal_callout_get_ticks(struct ble_npl_callout *co)
{ return espb_npl_funcs.p_ble_npl_callout_get_ticks(co); }
uint32_t wr_btdm_osal_callout_remaining_ticks(struct ble_npl_callout *co, uint32_t time)
{ return espb_npl_funcs.p_ble_npl_callout_remaining_ticks(co, time); }
void wr_btdm_osal_callout_set_arg(struct ble_npl_callout *co, void *arg)
{ espb_npl_funcs.p_ble_npl_callout_set_arg(co, arg); }
uint32_t wr_btdm_osal_time_get(void)
{ return espb_npl_funcs.p_ble_npl_time_get(); }
int wr_btdm_osal_time_ms_to_ticks(uint32_t ms, uint32_t *out_ticks)
{ return espb_npl_funcs.p_ble_npl_time_ms_to_ticks(ms, out_ticks); }
int wr_btdm_osal_time_ticks_to_ms(uint32_t ticks, uint32_t *out_ms)
{ return espb_npl_funcs.p_ble_npl_time_ticks_to_ms(ticks, out_ms); }
uint32_t wr_btdm_osal_time_ms_to_ticks32(uint32_t ms)
{ return espb_npl_funcs.p_ble_npl_time_ms_to_ticks32(ms); }
uint32_t wr_btdm_osal_time_ticks_to_ms32(uint32_t ticks)
{ return espb_npl_funcs.p_ble_npl_time_ticks_to_ms32(ticks); }
uint32_t wr_btdm_osal_hw_enter_critical(void)
{ return espb_npl_funcs.p_ble_npl_hw_enter_critical(); }
void wr_btdm_osal_hw_exit_critical(uint32_t ctx)
{ espb_npl_funcs.p_ble_npl_hw_exit_critical(ctx); }
uint8_t wr_btdm_osal_hw_is_in_critical(void)
{ return espb_npl_funcs.p_ble_npl_hw_is_in_critical(); }
uint32_t wr_btdm_osal_get_time_forever(void)
{ return espb_npl_funcs.p_ble_npl_get_time_forever(); }
static unsigned pools_live;
int btdm_osal_elem_mempool_init(espb_npl_counts_t *counts)
{
    if (!counts || pools_live || tiku_s31_in_trap || !tiku_thread_in_kernel() ||
        (unsigned)counts->evt_count + counts->co_count > UINT16_MAX) return -1;
    if (espb_npl_init(counts)) return -1;
    pools_live = 1;
    return 0;
}
void btdm_osal_elem_mempool_deinit(void)
{
    if (tiku_s31_in_trap || !tiku_thread_in_kernel()) tiku_s31_fatal("BTDM pool context");
    if (pools_live) { espb_npl_deinit(); pools_live = 0; }
}
int wr_btdm_osal_task_create(void (*fn)(void *), const char *name, uint32_t stack,
                            void *arg, uint32_t priority, void **handle, uint32_t core)
{
    if (handle) *handle = NULL;
    if (!fn || !name || stack < 1024 || stack > 32768 || priority > 24 ||
        (core != 0 && core != UINT32_MAX) || tiku_s31_in_trap) return BLE_NPL_EINVAL;
    return espw_osi_funcs._task_create((void *)fn, name, stack, arg, priority, handle)
        ? BLE_NPL_OK : BLE_NPL_ERROR;
}
void wr_btdm_osal_task_delete(void *handle)
{
    espw_osi_funcs._task_delete(handle);
    if (handle && handle != (void *)tiku_thread_self())
        (void)tiku_thread_join((tiku_thread_t *)handle);
}
static struct { void (*fn)(void *); void *arg; } irqs[4];
#define BTDM_IRQ(n) static void irq_##n(void) { irqs[n].fn(irqs[n].arg); }
BTDM_IRQ(0)
BTDM_IRQ(1)
BTDM_IRQ(2)
BTDM_IRQ(3)
static void (*const entries[4])(void) = {irq_0, irq_1, irq_2, irq_3};
int wr_btdm_osal_intr_alloc(int source, int flags, void (*fn)(void *), void *arg, void **handle)
{
    (void)flags;
    if (handle) *handle = NULL;
    if (!handle || !fn || source < 0 || source >= 169 || tiku_s31_in_trap) return -1;
    for (unsigned i = 0; i < 4; i++) if (!irqs[i].fn) {
        if (tiku_s31_irq_claim(28+i, source, 3, entries[i])) return -1;
        irqs[i].fn = fn; irqs[i].arg = arg;
        *handle = &irqs[i];
        if (tiku_s31_irq_enable(28+i, 1)) tiku_s31_fatal("BTDM IRQ enable");
        return 0;
    }
    return -1;
}
int wr_btdm_osal_intr_free(void *handle)
{
    for (unsigned i = 0; i < 4; i++) if (handle == &irqs[i] && irqs[i].fn) {
        if (tiku_s31_irq_release(28+i, entries[i])) return -1;
        irqs[i].fn = NULL; irqs[i].arg = NULL;
        return 0;
    }
    return -1;
}
void *wr_btdm_osal_malloc(uint32_t size, uint32_t flags)
{ return flags & ~0x301u ? NULL : espw_malloc(size); }
void wr_btdm_osal_free(void *memory) { espw_free(memory); }
/* The S31 controller has no C61 host-pacing handshake. */
void espb_controller_idle(void) { }

int wr_btdm_osal_read_efuse_mac(uint8_t *mac)
{ return espw_read_mac(mac, ESP_MAC_BT); }
void wr_btdm_osal_srand(uint32_t seed) { (void)seed; }
int wr_btdm_osal_rand(void)
{
    uint32_t value;
    if (espw_s31_random((uint8_t *)&value, sizeof value)) tiku_s31_fatal("BTDM random source");
    return (int)value;
}
