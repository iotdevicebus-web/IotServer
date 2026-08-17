/**
 * @file app_events.h
 * @brief エッジアプリケーション イベント定義
 */

#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EVENT_TYPE_NONE = 0,
    EVENT_TYPE_WAKEUP_TIMER,        /**< 定期起床タイマー発火 */
    EVENT_TYPE_GPIO_INTERRUPT,      /**< ボタン押下 / 外部センサ割り込み */
    EVENT_TYPE_NETWORK_CONNECTED,   /**< ネットワーク接続成功 */
    EVENT_TYPE_NETWORK_DISCONNECTED,/**< ネットワーク切断 */
    EVENT_TYPE_TELEMETRY_TX_DONE,   /**< テレメトリ送信完了 */
    EVENT_TYPE_OTA_AVAILABLE,       /**< OTAアップデート要求検知 */
    EVENT_TYPE_SYSTEM_ERROR         /**< ハードウェア・通信エラー */
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    uint32_t timestamp;
    union {
        struct {
            uint32_t pin;
            bool level;
        } gpio_data;
        struct {
            int http_status;
            uint32_t sleep_interval_sec;
            bool ota_pending;
        } tx_result;
        struct {
            int error_code;
            const char *error_msg;
        } error_data;
    } payload;
} app_event_msg_t;

#ifdef __cplusplus
}
#endif

#endif // APP_EVENTS_H
