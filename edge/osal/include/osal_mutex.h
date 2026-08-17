/**
 * @file osal_mutex.h
 * @brief OSAL 排他制御 (ミューテックス / セマフォ) インターフェース
 */

#ifndef OSAL_MUTEX_H
#define OSAL_MUTEX_H

#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* osal_mutex_handle_t;
typedef void* osal_sem_handle_t;

/* --- Mutex --- */
osal_status_t osal_mutex_create(osal_mutex_handle_t *out_mutex);
osal_status_t osal_mutex_lock(osal_mutex_handle_t mutex, uint32_t timeout_ms);
osal_status_t osal_mutex_unlock(osal_mutex_handle_t mutex);
osal_status_t osal_mutex_delete(osal_mutex_handle_t mutex);

/* --- Counting / Binary Semaphore --- */
osal_status_t osal_sem_create(uint32_t max_count, uint32_t initial_count, osal_sem_handle_t *out_sem);
osal_status_t osal_sem_take(osal_sem_handle_t sem, uint32_t timeout_ms);
osal_status_t osal_sem_give(osal_sem_handle_t sem);
osal_status_t osal_sem_give_from_isr(osal_sem_handle_t sem, bool *higher_priority_task_woken);
osal_status_t osal_sem_delete(osal_sem_handle_t sem);

#ifdef __cplusplus
}
#endif

#endif // OSAL_MUTEX_H
