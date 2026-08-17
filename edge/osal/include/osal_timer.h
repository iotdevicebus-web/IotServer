/**
 * @file osal_timer.h
 * @brief OSAL ソフトウェアタイマーインターフェース
 */

#ifndef OSAL_TIMER_H
#define OSAL_TIMER_H

#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* osal_timer_handle_t;
typedef void (*osal_timer_callback_t)(void *arg);

typedef enum {
    OSAL_TIMER_ONE_SHOT,
    OSAL_TIMER_PERIODIC
} osal_timer_mode_t;

osal_status_t osal_timer_create(
    const char *name,
    uint32_t period_ms,
    osal_timer_mode_t mode,
    osal_timer_callback_t callback,
    void *arg,
    osal_timer_handle_t *out_timer
);

osal_status_t osal_timer_start(osal_timer_handle_t timer, uint32_t timeout_ms);
osal_status_t osal_timer_stop(osal_timer_handle_t timer, uint32_t timeout_ms);
osal_status_t osal_timer_change_period(osal_timer_handle_t timer, uint32_t new_period_ms, uint32_t timeout_ms);
osal_status_t osal_timer_delete(osal_timer_handle_t timer);

#ifdef __cplusplus
}
#endif

#endif // OSAL_TIMER_H
