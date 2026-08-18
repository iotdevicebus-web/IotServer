/**
 * @file AppConst.hpp
 * @brief ESP32-S3 アプリケーション共通定数定義 (マジックナンバー排除)
 */
#ifndef APP_CONST_HPP
#define APP_CONST_HPP

#include <stdint.h>
#include <stddef.h>
#include <time.h>

namespace AppConst {

// --- ハードウェア GPIO ピン設定 ---
constexpr uint8_t PIN_WAKEUP_BUTTON = 4;    // 外部起床スイッチ (RTC_IO Active LOW)
constexpr uint8_t PIN_STATUS_LED    = 48;   // オンボードステータス LED (GPIO 48)
constexpr uint32_t SERIAL_BAUDRATE  = 115200;

// --- Waveshare 1.54inch e-Paper Module Rev2.1 GPIO ピン設定 ---
constexpr uint8_t PIN_EPD_BUSY      = 7;    // BUSY 状態入力 (Busy: HIGH, Idle: LOW)
constexpr uint8_t PIN_EPD_RST       = 8;    // リセット (Active LOW)
constexpr uint8_t PIN_EPD_DC        = 9;    // コマンド/データ選択 (Data: HIGH, Command: LOW)
constexpr uint8_t PIN_EPD_CS        = 10;   // SPI チップセレクト (Active LOW / FSPI CS)
constexpr uint8_t PIN_EPD_MOSI      = 11;   // SPI MOSI / DIN (FSPI MOSI)
constexpr uint8_t PIN_EPD_SCK       = 12;   // SPI SCK / CLK (FSPI SCK)
constexpr uint16_t EPD_WIDTH        = 200;  // 画面幅 (ピクセル)
constexpr uint16_t EPD_HEIGHT       = 200;  // 画面高さ (ピクセル)


// --- 8MB PSRAM / バッファ設定 ---
constexpr size_t PSRAM_BUFFER_CAPACITY   = 10000; // 8MB PSRAM 保持件数 (~1.5MB)
constexpr size_t SRAM_BUFFER_FALLBACK    = 64;    // 内部 SRAM フォールバック件数

// --- ネットワーク & サーバ設定 ---
constexpr uint16_t SERVER_PORT           = 8443;
constexpr uint32_t WIFI_TIMEOUT_MS       = 10000; // イベント駆動待機タイムアウト (10秒)
constexpr const char* SERVER_HOST        = "192.168.3.4";
constexpr const char* WIFI_SSID          = "ControlAdLab";
constexpr const char* WIFI_PASSWORD      = "ControlAD";

// --- 時刻同期 & スリープ設定 ---
constexpr time_t FAST_CLOCK_INIT_TIMESTAMP = 1786968000; // 2026-08-17 12:00:00 UTC (0msセット)
constexpr uint32_t DEEP_SLEEP_DURATION_SEC = 15;        // スリープ周期 (15秒)

} // namespace AppConst

#endif // APP_CONST_HPP
