/**
 * @file hal_epaper.h
 * @brief E-Paper (電子ペーパー) ハードウェア抽象化レイヤー (HAL) インターフェース
 */

#ifndef HAL_EPAPER_H
#define HAL_EPAPER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief e-Paper 表示用ステータス構造体
 */
typedef struct {
    const char* device_id;       // デバイスID (例: DEV-ESP32-001)
    const char* ip_address;      // IPアドレス (例: 192.168.3.65)
    uint32_t boot_count;         // 起動回数
    uint32_t interval_sec;       // スリープ間隔 (秒)
    float temperature;           // 温度 (℃)
    float humidity;              // 湿度 (%)
    float battery_voltage;       // バッテリー電圧 (V)
    const char* server_status;   // 通信状態 (例: 200 OK, CONNECTING, etc.)
    const char* time_jst_str;    // 日本標準時 (JST) 時刻文字列 (例: "2026/08/19 14:05:22")
} epd_status_info_t;


/**
 * @brief e-Paper ディスプレイの初期化
 * @return true 成功, false 失敗
 */
bool hal_epaper_init(void);

/**
 * @brief テスト描画画面を表示 (幾何学パターン、文字、枠線等の動作確認)
 */
void hal_epaper_show_test_screen(void);

/**
 * @brief IoT デバイスの動作ステータス画面を表示
 * @param info 表示するステータス情報
 */
void hal_epaper_show_status(const epd_status_info_t* info);

/**
 * @brief e-Paper 画面を白で全画面クリア
 */
void hal_epaper_clear(void);

/**
 * @brief e-Paper コントローラを Deep Sleep (超低消費電力モード) に移行
 */
void hal_epaper_sleep(void);

#ifdef __cplusplus
}
#endif

#endif // HAL_EPAPER_H
