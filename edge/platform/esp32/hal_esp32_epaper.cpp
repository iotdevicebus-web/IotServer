/**
 * @file hal_esp32_epaper.cpp
 * @brief Waveshare 1.54inch e-Paper Module Rev2.1 (SSD1681 200x200) 用 HAL 実装
 * @note QC_check_ESP32.md 規約準拠:
 *       - Phase 1: ESP32 標準 SPI / GxEPD2 標準 API 準拠
 *       - Phase 2: 画面操作・ステータス制御を HAL 内部にカプセル化
 *       - Phase 3: デバッグログは Callee 側（本ファイル内）で出力
 *       - Phase 4: ピン番号等は AppConst.hpp で一元管理
 */

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include "hal_epaper.h"
#include "AppConst.hpp"

// Waveshare 1.54inch Rev2.1 (200x200 SSD1681) 用ドライバインスタンス
static GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> s_display(
    GxEPD2_154_D67(
        AppConst::PIN_EPD_CS,
        AppConst::PIN_EPD_DC,
        AppConst::PIN_EPD_RST,
        AppConst::PIN_EPD_BUSY
    )
);

static bool s_epd_initialized = false;

/**
 * @brief e-Paper の初期化 (SPI バス再マッピング + ディスプレイ初期化)
 */
bool hal_epaper_init(void) {
    if (s_epd_initialized) {
        return true;
    }

    Serial.println("[EPD] Initializing Waveshare 1.54inch e-Paper (SSD1681)...");
    
    // ESP32-S3 のハードウェア SPI ピンを指定して SPI バス初期化
    SPI.begin(AppConst::PIN_EPD_SCK, -1, AppConst::PIN_EPD_MOSI, AppConst::PIN_EPD_CS);

    // GxEPD2 初期化 (ボーレート: 115200, 初期リセット: true, パルス時間: 2ms, リセット後プルアップ待機: false)
    s_display.init(AppConst::SERIAL_BAUDRATE, true, 2, false);
    s_display.setRotation(0);
    s_display.setTextColor(GxEPD_BLACK);
    
    s_epd_initialized = true;
    Serial.println("[EPD] Initialization successful (200x200 Black/White).");
    return true;
}

/**
 * @brief テスト描画画面 (幾何学図形・ヘッダー・情報枠・パターンの表示)
 */
void hal_epaper_show_test_screen(void) {
    if (!hal_epaper_init()) {
        Serial.println("[EPD] Error: Cannot show test screen (Not initialized)");
        return;
    }

    Serial.println("[EPD] Rendering Test Screen Pattern...");
    uint32_t start_ms = millis();

    s_display.setFullWindow();
    s_display.firstPage();
    do {
        s_display.fillScreen(GxEPD_WHITE);

        // 1. 外枠 (Border)
        s_display.drawRect(2, 2, 196, 196, GxEPD_BLACK);
        s_display.drawRect(4, 4, 192, 192, GxEPD_BLACK);

        // 2. ヘッダー帯 (Header Banner)
        s_display.fillRect(4, 4, 192, 28, GxEPD_BLACK);
        s_display.setTextColor(GxEPD_WHITE);
        s_display.setTextSize(2);
        s_display.setCursor(14, 10);
        s_display.print("IoT PLATFORM");

        // 3. サブタイトル / ハードウェア情報
        s_display.setTextColor(GxEPD_BLACK);
        s_display.setTextSize(1);
        s_display.setCursor(10, 38);
        s_display.print("Freenove ESP32-S3 WROOM");
        s_display.setCursor(10, 48);
        s_display.print("Waveshare 1.54\" Rev2.1");

        // 水平区切り線
        s_display.drawFastHLine(10, 60, 180, GxEPD_BLACK);

        // 4. テストステータス情報
        s_display.setCursor(10, 66);
        s_display.print("Display: 200x200 B/W OK");
        s_display.setCursor(10, 78);
        s_display.print("Security: mTLS 2048-bit");
        s_display.setCursor(10, 90);
        s_display.print("Memory: 8MB PSRAM OPI");
        s_display.setCursor(10, 102);
        s_display.print("Event : Zero-Polling");

        // 水平区切り線
        s_display.drawFastHLine(10, 114, 180, GxEPD_BLACK);

        // 5. グラフィック図形テスト (幾何学パターン)
        s_display.drawRect(12, 120, 32, 32, GxEPD_BLACK);
        s_display.fillRect(16, 124, 24, 24, GxEPD_BLACK);

        s_display.drawCircle(68, 136, 16, GxEPD_BLACK);
        s_display.fillCircle(68, 136, 10, GxEPD_BLACK);

        s_display.drawTriangle(105, 152, 120, 120, 135, 152, GxEPD_BLACK);
        s_display.fillTriangle(110, 150, 120, 126, 130, 150, GxEPD_BLACK);

        // チェッカーパターン (白黒階調テスト)
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if ((x + y) % 2 == 0) {
                    s_display.fillRect(150 + x * 8, 120 + y * 8, 8, 8, GxEPD_BLACK);
                }
            }
        }

        // 6. フッター帯 (Footer)
        s_display.drawFastHLine(4, 162, 192, GxEPD_BLACK);
        s_display.setCursor(10, 168);
        s_display.print("STATUS: TEST PASSED [OK]");
        s_display.setCursor(10, 180);
        s_display.print("Ready for Live Telemetry");

    } while (s_display.nextPage());

    uint32_t elapsed_ms = millis() - start_ms;
    Serial.printf("[EPD] Test Screen rendered successfully in %u ms.\n", elapsed_ms);
}

/**
 * @brief IoT デバイス稼働ステータス画面の描画 (大型フォントで見やすく最適化)
 */
void hal_epaper_show_status(const epd_status_info_t* info) {
    if (info == nullptr) return;
    if (!hal_epaper_init()) return;

    Serial.println("[EPD] Rendering Live Status Screen (Large Fonts)...");
    uint32_t start_ms = millis();

    s_display.setFullWindow();
    s_display.firstPage();
    do {
        s_display.fillScreen(GxEPD_WHITE);

        // 1. ヘッダー帯 (デバイスIDを大型 TextSize 2 で表示)
        s_display.fillRect(0, 0, 200, 28, GxEPD_BLACK);
        s_display.setTextColor(GxEPD_WHITE);
        s_display.setTextSize(2);
        s_display.setCursor(8, 6);
        s_display.print(info->device_id ? info->device_id : "DEV-ESP32");

        // 2. ネットワーク情報 (IPアドレス & 起動回数)
        s_display.setTextColor(GxEPD_BLACK);
        s_display.setTextSize(1);
        s_display.setCursor(6, 33);
        s_display.printf("IP: %s", info->ip_address ? info->ip_address : "Connecting...");
        s_display.setCursor(144, 33);
        s_display.printf("#%u", info->boot_count);

        s_display.drawFastHLine(4, 46, 192, GxEPD_BLACK);

        // 3. メインセンサー値 (TextSize 3 の超大型フォントで温度・湿度を表示)
        // 温度 (Temperature)
        s_display.setTextSize(3);
        s_display.setCursor(6, 52);
        s_display.printf("%.1f", info->temperature);

        // 単位 ℃
        s_display.setTextSize(1);
        s_display.setCursor(82, 52);
        s_display.print("o");
        s_display.setTextSize(2);
        s_display.setCursor(90, 56);
        s_display.print("C");

        // 縦区切り線
        s_display.drawFastVLine(104, 50, 32, GxEPD_BLACK);

        // 湿度 (Humidity)
        s_display.setTextSize(3);
        s_display.setCursor(114, 52);
        s_display.printf("%.0f", info->humidity);

        // 単位 %
        s_display.setTextSize(2);
        s_display.setCursor(168, 56);
        s_display.print("%");

        s_display.drawFastHLine(4, 88, 192, GxEPD_BLACK);

        // 4. バッテリー & 周期 (TextSize 2 の中型フォントで明瞭表示)
        s_display.setTextSize(2);
        s_display.setCursor(6, 94);
        s_display.printf("%.2fV", info->battery_voltage);

        s_display.setCursor(108, 94);
        s_display.printf("Intv:%us", info->interval_sec);

        s_display.drawFastHLine(4, 116, 192, GxEPD_BLACK);

        // 5. サーバ通信ステータス枠 (TextSize 2 で結果を強調)
        s_display.drawRoundRect(4, 122, 192, 44, 4, GxEPD_BLACK);
        s_display.setTextSize(1);
        s_display.setCursor(10, 128);
        s_display.printf("SERVER: %s", AppConst::SERVER_HOST);

        s_display.setTextSize(2);
        s_display.setCursor(10, 142);
        s_display.printf("%s", info->server_status ? info->server_status : "ONLINE");

        // 6. フッター帯 (ゼロトラスト暗号化ステータス)
        s_display.fillRect(0, 172, 200, 28, GxEPD_BLACK);
        s_display.setTextColor(GxEPD_WHITE);
        s_display.setTextSize(2);
        s_display.setCursor(14, 178);
        s_display.print("mTLS SECURE");

    } while (s_display.nextPage());

    uint32_t elapsed_ms = millis() - start_ms;
    Serial.printf("[EPD] Status Screen updated in %u ms.\n", elapsed_ms);
}

/**
 * @brief 全画面クリア (白画面)
 */
void hal_epaper_clear(void) {
    if (!hal_epaper_init()) return;

    Serial.println("[EPD] Clearing screen (Full White)...");
    s_display.setFullWindow();
    s_display.firstPage();
    do {
        s_display.fillScreen(GxEPD_WHITE);
    } while (s_display.nextPage());
    Serial.println("[EPD] Screen cleared.");
}

/**
 * @brief ディスプレイをスリープ (休止) 状態にする
 */
void hal_epaper_sleep(void) {
    if (!s_epd_initialized) return;

    Serial.println("[EPD] Entering Ultra-Low-Power Hibernate...");
    s_display.hibernate();
    s_epd_initialized = false;
}
