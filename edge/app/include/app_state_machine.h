/**
 * @file app_state_machine.h
 * @brief エッジ省電力イベント駆動ステートマシン インターフェース
 */

#ifndef APP_STATE_MACHINE_H
#define APP_STATE_MACHINE_H

#include "app_events.h"
#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATE_BOOT = 0,             /**< 起動・初期化 */
    STATE_SENSING,              /**< センシング & バッテリ計測 */
    STATE_CONNECTING,           /**< ネットワーク接続 */
    STATE_TRANSMITTING,         /**< mTLS HTTPS POST 送信 */
    STATE_OTA_PROCESSING,       /**< OTAダウンロード & 検証 */
    STATE_PREPARE_SLEEP,        /**< スリープ移行準備 (次回タイマー設定) */
    STATE_SLEEPING              /**< Deep / Light Sleep 状態 */
} app_state_t;

/**
 * @brief アプリケーションステートマシンの初期化
 */
osal_status_t app_state_machine_init(void);

/**
 * @brief イベントを投入して状態遷移・処理を実行する
 */
osal_status_t app_state_machine_dispatch(const app_event_msg_t *event);

/**
 * @brief 現在の状態を取得する
 */
app_state_t app_state_machine_get_current_state(void);

/**
 * @brief メインイベントループ (OSALタスク内から実行)
 */
void app_main_event_loop(void *arg);

#ifdef __cplusplus
}
#endif

#endif // APP_STATE_MACHINE_H
