/**
 * @file osal_queue.h
 * @brief OSAL メッセージキュー・インターフェース (イベント駆動の中核)
 */

#ifndef OSAL_QUEUE_H
#define OSAL_QUEUE_H

#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* osal_queue_handle_t;

/**
 * @brief メッセージキューを生成する
 * @param queue_length キューの最大格納メッセージ数
 * @param item_size 1メッセージあたりのバイトサイズ
 * @param out_queue 生成されたキューハンドル
 */
osal_status_t osal_queue_create(
    uint32_t queue_length,
    size_t item_size,
    osal_queue_handle_t *out_queue
);

/**
 * @brief キューへメッセージを送信する (通常コンテキスト用)
 */
osal_status_t osal_queue_send(
    osal_queue_handle_t queue,
    const void *item,
    uint32_t timeout_ms
);

/**
 * @brief 割り込みサービスルーチン (ISR) 内からキューへメッセージを送信する
 */
osal_status_t osal_queue_send_from_isr(
    osal_queue_handle_t queue,
    const void *item,
    bool *higher_priority_task_woken
);

/**
 * @brief キューからメッセージを受信する (ブロッキング待機可能)
 */
osal_status_t osal_queue_receive(
    osal_queue_handle_t queue,
    void *buffer,
    uint32_t timeout_ms
);

/**
 * @brief キューを破棄・解放する
 */
osal_status_t osal_queue_delete(osal_queue_handle_t queue);

#ifdef __cplusplus
}
#endif

#endif // OSAL_QUEUE_H
