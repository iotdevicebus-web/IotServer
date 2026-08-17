/**
 * @file osal.h
 * @brief OSAL (OS Abstraction Layer) 総合ヘッダ
 */

#ifndef OSAL_H
#define OSAL_H

#include "osal_types.h"
#include "osal_task.h"
#include "osal_queue.h"
#include "osal_mutex.h"
#include "osal_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OSALおよび下位RTOSカーネルの初期化
 */
osal_status_t osal_init(void);

/**
 * @brief RTOSスケジューラの開始 (起動完了後は基本的に復帰しない)
 */
void osal_start_scheduler(void);

#ifdef __cplusplus
}
#endif

#endif // OSAL_H
