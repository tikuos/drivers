/*
 * Tiku Drivers
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * btdm_s31_osal.h - S31 BTDM entry points using shared NPL object layouts.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BTDM_S31_OSAL_H_
#define TIKU_BTDM_S31_OSAL_H_
#include "../esp_ble_abi.h"
/* BTDM's five pointer handles, error values and millisecond ticks match NPL. */
/** @brief Forward the BTDM eventq init operation to NPL. */
void wr_btdm_osal_eventq_init(struct ble_npl_eventq *evq);
/** @brief Forward the BTDM eventq deinit operation to NPL. */
void wr_btdm_osal_eventq_deinit(struct ble_npl_eventq *evq);
/** @brief Forward the BTDM eventq get operation to NPL. */
struct ble_npl_event *wr_btdm_osal_eventq_get(struct ble_npl_eventq *evq, uint32_t tmo);
/** @brief Forward the BTDM eventq put operation to NPL. */
void wr_btdm_osal_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev);
/** @brief Forward the BTDM eventq put to front operation to NPL. */
void wr_btdm_osal_eventq_put_to_front(struct ble_npl_eventq *evq, struct ble_npl_event *ev);
/** @brief Forward the BTDM eventq remove operation to NPL. */
void wr_btdm_osal_eventq_remove(struct ble_npl_eventq *evq, struct ble_npl_event *ev);
/** @brief Forward the BTDM eventq is empty operation to NPL. */
bool wr_btdm_osal_eventq_is_empty(struct ble_npl_eventq *evq);
/** @brief Forward the BTDM event run operation to NPL. */
void wr_btdm_osal_event_run(struct ble_npl_event *ev);
/** @brief Forward the BTDM event init operation to NPL. */
void wr_btdm_osal_event_init(struct ble_npl_event *ev, ble_npl_event_fn *fn, void *arg);
/** @brief Forward the BTDM event deinit operation to NPL. */
void wr_btdm_osal_event_deinit(struct ble_npl_event *ev);
/** @brief Forward the BTDM event reset operation to NPL. */
void wr_btdm_osal_event_reset(struct ble_npl_event *ev);
/** @brief Forward the BTDM event is queued operation to NPL. */
bool wr_btdm_osal_event_is_queued(struct ble_npl_event *ev);
/** @brief Forward the BTDM event get arg operation to NPL. */
void *wr_btdm_osal_event_get_arg(struct ble_npl_event *ev);
/** @brief Forward the BTDM event set arg operation to NPL. */
void wr_btdm_osal_event_set_arg(struct ble_npl_event *ev, void *arg);
/** @brief Forward the BTDM mutex init operation to NPL. */
int wr_btdm_osal_mutex_init(struct ble_npl_mutex *mu);
/** @brief Forward the BTDM mutex deinit operation to NPL. */
int wr_btdm_osal_mutex_deinit(struct ble_npl_mutex *mu);
/** @brief Forward the BTDM mutex pend operation to NPL. */
int wr_btdm_osal_mutex_pend(struct ble_npl_mutex *mu, uint32_t timeout);
/** @brief Forward the BTDM mutex release operation to NPL. */
int wr_btdm_osal_mutex_release(struct ble_npl_mutex *mu);
/** @brief Forward the BTDM sem init operation to NPL. */
int wr_btdm_osal_sem_init(struct ble_npl_sem *sem, uint16_t tokens);
/** @brief Forward the BTDM sem deinit operation to NPL. */
int wr_btdm_osal_sem_deinit(struct ble_npl_sem *sem);
/** @brief Forward the BTDM sem pend operation to NPL. */
int wr_btdm_osal_sem_pend(struct ble_npl_sem *sem, uint32_t timeout);
/** @brief Forward the BTDM sem release operation to NPL. */
int wr_btdm_osal_sem_release(struct ble_npl_sem *sem);
/** @brief Forward the BTDM sem get count operation to NPL. */
uint16_t wr_btdm_osal_sem_get_count(struct ble_npl_sem *sem);
/** @brief Forward the BTDM callout init operation to NPL. */
int wr_btdm_osal_callout_init(struct ble_npl_callout *co, struct ble_npl_eventq *evq, ble_npl_event_fn *ev_cb, void *ev_arg);
/** @brief Forward the BTDM callout deinit operation to NPL. */
void wr_btdm_osal_callout_deinit(struct ble_npl_callout *co);
/** @brief Forward the BTDM callout reset operation to NPL. */
int wr_btdm_osal_callout_reset(struct ble_npl_callout *co, uint32_t ticks);
/** @brief Forward the BTDM callout mem reset operation to NPL. */
void wr_btdm_osal_callout_mem_reset(struct ble_npl_callout *co);
/** @brief Forward the BTDM callout stop operation to NPL. */
void wr_btdm_osal_callout_stop(struct ble_npl_callout *co);
/** @brief Forward the BTDM callout is active operation to NPL. */
bool wr_btdm_osal_callout_is_active(struct ble_npl_callout *co);
/** @brief Forward the BTDM callout get ticks operation to NPL. */
uint32_t wr_btdm_osal_callout_get_ticks(struct ble_npl_callout *co);
/** @brief Forward the BTDM callout remaining ticks operation to NPL. */
uint32_t wr_btdm_osal_callout_remaining_ticks(struct ble_npl_callout *co, uint32_t time);
/** @brief Forward the BTDM callout set arg operation to NPL. */
void wr_btdm_osal_callout_set_arg(struct ble_npl_callout *co, void *arg);
/** @brief Forward the BTDM time get operation to NPL. */
uint32_t wr_btdm_osal_time_get(void);
/** @brief Forward the BTDM time ms to ticks operation to NPL. */
int wr_btdm_osal_time_ms_to_ticks(uint32_t ms, uint32_t *out_ticks);
/** @brief Forward the BTDM time ticks to ms operation to NPL. */
int wr_btdm_osal_time_ticks_to_ms(uint32_t ticks, uint32_t *out_ms);
/** @brief Forward the BTDM time ms to ticks32 operation to NPL. */
uint32_t wr_btdm_osal_time_ms_to_ticks32(uint32_t ms);
/** @brief Forward the BTDM time ticks to ms32 operation to NPL. */
uint32_t wr_btdm_osal_time_ticks_to_ms32(uint32_t ticks);
/** @brief Forward the BTDM hw enter critical operation to NPL. */
uint32_t wr_btdm_osal_hw_enter_critical(void);
/** @brief Forward the BTDM hw exit critical operation to NPL. */
void wr_btdm_osal_hw_exit_critical(uint32_t ctx);
/** @brief Forward the BTDM hw is in critical operation to NPL. */
uint8_t wr_btdm_osal_hw_is_in_critical(void);
/** @brief Forward the BTDM get time forever operation to NPL. */
uint32_t wr_btdm_osal_get_time_forever(void);
/** @brief Allocate the requested pools once; reject repeat initialization or count overflow. */
int btdm_osal_elem_mempool_init(espb_npl_counts_t *counts);
/** @brief Disarm callouts and release the controller object pools. */
void btdm_osal_elem_mempool_deinit(void);
/** @brief Create a CPU0 worker; return BTDM_OK or an error, never a FreeRTOS handle. */
int wr_btdm_osal_task_create(void (*fn)(void *), const char *name, uint32_t stack,
                            void *arg, uint32_t priority, void **handle, uint32_t core);
/** @brief Delete a worker; wait for another worker to exit before returning. */
void wr_btdm_osal_task_delete(void *handle);
/** @brief Claim one of four BTDM IRQ slots; calls require CPU0 thread context. */
int wr_btdm_osal_intr_alloc(int source, int flags, void (*fn)(void *), void *arg, void **handle);
/** @brief Disable and release a live BTDM IRQ handle; return -1 for invalid handles. */
int wr_btdm_osal_intr_free(void *handle);
/** @brief Allocate internal radio memory; unsupported flag bits return NULL. */
void *wr_btdm_osal_malloc(uint32_t size, uint32_t flags);
/** @brief Release radio memory; NULL is accepted. */
void wr_btdm_osal_free(void *memory);
/** @brief Read the Bluetooth MAC through the platform address provider. */
int wr_btdm_osal_read_efuse_mac(uint8_t *mac);
/** @brief Accept a seed without changing the hardware random source. */
void wr_btdm_osal_srand(uint32_t seed);
/** @brief Return hardware random bits; halt if the source reports a fault. */
int wr_btdm_osal_rand(void);
#endif
