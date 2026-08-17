/**
 * @file osal_task.h
 * @brief OSAL タスク・スレッド管理インターフェース
 */

#ifndef OSAL_TASK_H
#define OSAL_TASK_H

#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* osal_task_handle_t;
typedef void (*osal_task_entry_t)(void *arg);

typedef enum {
    OSAL_PRIORITY_IDLE     = 0,
    OSAL_PRIORITY_LOW      = 1,
    OSAL_PRIORITY_NORMAL   = 2,
    OSAL_PRIORITY_HIGH     = 3,
    OSAL_PRIORITY_REALTIME = 4
} osal_priority_t;

typedef struct {
    const char *name;
    uint32_t stack_size;
    osal_priority_t priority;
} osal_task_config_t;

/**
 * @brief タスクを生成する
 */
osal_status_t osal_task_create(
    const osal_task_config_t *config,
    osal_task_entry_t entry_func,
    void *arg,
    osal_task_handle_t *out_handle
);

/**
 * @brief タスクを削除・終了する
 */
osal_status_t osal_task_delete(osal_task_handle_t handle);

/**
 * @brief 自タスクを指定ミリ秒スリープ（ディレイ）させる
 */
void osal_task_delay_ms(uint32_t ms);

/**
 * @brief システム起動後の経過ミリ秒を取得する
 */
uint32_t osal_get_time_ms(void);

#ifdef __cplusplus
}
#endif

#endif // OSAL_TASK_H
