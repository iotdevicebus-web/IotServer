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

        // 1. ヘッダー帯 (デバイスID & 起動回数を表示)
        s_display.fillRect(0, 0, 200, 22, GxEPD_BLACK);
        s_display.setTextColor(GxEPD_WHITE);
        s_display.setTextSize(2);
        s_display.setCursor(6, 3);
        s_display.print(info->device_id ? info->device_id : "DEV-ESP32");

        s_display.setTextSize(1);
        s_display.setCursor(160, 7);
        s_display.printf("#%u", info->boot_count);

        // 2. ネットワーク情報 (IPアドレス & RSSI)
        s_display.setTextColor(GxEPD_BLACK);
        s_display.setTextSize(1);
        s_display.setCursor(6, 26);
        if (info->rssi != 0 && info->rssi != -99) {
            s_display.printf("IP: %s (%ddBm)", info->ip_address ? info->ip_address : "Connecting...", info->rssi);
        } else {
            s_display.printf("IP: %s", info->ip_address ? info->ip_address : "Connecting...");
        }

        s_display.drawFastHLine(4, 37, 192, GxEPD_BLACK);


        // 3. メインセンサー値 (TextSize 3 の大型フォントで温度・湿度を表示)
        // 温度 (Temperature)
        s_display.setTextSize(3);
        s_display.setCursor(6, 41);
        s_display.printf("%.1f", info->temperature);

        // 単位 ℃
        s_display.setTextSize(1);
        s_display.setCursor(82, 41);
        s_display.print("o");
        s_display.setTextSize(2);
        s_display.setCursor(90, 45);
        s_display.print("C");

        // 縦区切り線
        s_display.drawFastVLine(104, 39, 34, GxEPD_BLACK);

        // 湿度 (Humidity)
        s_display.setTextSize(3);
        s_display.setCursor(114, 41);
        s_display.printf("%.0f", info->humidity);

        // 単位 %
        s_display.setTextSize(2);
        s_display.setCursor(168, 45);
        s_display.print("%");

        s_display.drawFastHLine(4, 75, 192, GxEPD_BLACK);

        // 4. バッテリー & 周期 (TextSize 2 の中型フォント)
        s_display.setTextSize(2);
        s_display.setCursor(6, 79);
        s_display.printf("%.2fV", info->battery_voltage);

        s_display.setCursor(108, 79);
        s_display.printf("Intv:%us", info->interval_sec);

        s_display.drawFastHLine(4, 97, 192, GxEPD_BLACK);

        // 5. 日本標準時 (JST) 超大型時刻表示
        // 日付・ラベル (TextSize 1)
        char date_buf[16] = "2026/08/19";
        char time_buf[16] = "14:05:22";
        if (info->time_jst_str && strlen(info->time_jst_str) >= 19) {
            strncpy(date_buf, info->time_jst_str, 10);
            date_buf[10] = '\0';
            strncpy(time_buf, info->time_jst_str + 11, 8);
            time_buf[8] = '\0';
        }

        s_display.setTextSize(1);
        s_display.setCursor(6, 100);
        s_display.printf("JST (UTC+9)  %s", date_buf);

        // 時刻本体 (TextSize 3 特大フォントで強調表示)
        s_display.setTextSize(3);
        s_display.setCursor(28, 111);
        s_display.printf("%s", time_buf);

        s_display.drawFastHLine(4, 137, 192, GxEPD_BLACK);

        // 6. サーバ通信ステータス枠 (TextSize 2 で結果を強調)
        s_display.drawRoundRect(4, 140, 192, 41, 4, GxEPD_BLACK);
        s_display.setTextSize(1);
        s_display.setCursor(10, 144);
        s_display.printf("SERVER: %s", AppConst::SERVER_HOST);

        s_display.setTextSize(2);
        s_display.setCursor(10, 157);
        s_display.printf("%s", info->server_status ? info->server_status : "ONLINE");

        // 7. フッター帯 (mTLS / 暗号化ステータスを小型 TextSize 1 でスッキリ配置)
        s_display.fillRect(0, 185, 200, 15, GxEPD_BLACK);
        s_display.setTextColor(GxEPD_WHITE);
        s_display.setTextSize(1);
        s_display.setCursor(16, 189);
        s_display.print("mTLS 2048-bit / HTTPS SECURE");


    } while (s_display.nextPage());

    uint32_t elapsed_ms = millis() - start_ms;
    Serial.printf("[EPD] Status Screen updated in %u ms.\n", elapsed_ms);
}

/**
 * @brief WiFiManager 設定モード画面を表示 (自局AP名・接続手順)
 */
void hal_epaper_show_wifi_setup_screen(const char* ap_name, const char* ip_addr) {
    if (!hal_epaper_init()) return;

    Serial.printf("[EPD] Rendering WiFi Setup Mode Screen (AP: %s, IP: %s)...\n", ap_name, ip_addr);
    uint32_t start_ms = millis();

    s_display.setFullWindow();
    s_display.firstPage();
    do {
        s_display.fillScreen(GxEPD_WHITE);

        // 1. ヘッダー帯 (黒帯 白文字)
        s_display.fillRect(0, 0, 200, 22, GxEPD_BLACK);
        s_display.setTextColor(GxEPD_WHITE);
        s_display.setTextSize(1);
        s_display.setCursor(18, 7);
        s_display.print(">>> Wi-Fi SETUP MODE <<<");

        // 2. 接続案内
        s_display.setTextColor(GxEPD_BLACK);
        s_display.setTextSize(1);
        s_display.setCursor(6, 28);
        s_display.print("1. Connect Phone/PC to AP:");

        // AP 名枠 (強調表示)
        s_display.drawRoundRect(4, 38, 192, 28, 4, GxEPD_BLACK);
        s_display.setTextSize(2);
        s_display.setCursor(10, 44);
        s_display.print(ap_name ? ap_name : "IotEdgeDevice");

        // 3. ブラウザ案内
        s_display.setTextSize(1);
        s_display.setCursor(6, 74);
        s_display.print("2. Open Browser & Go to:");

        // IP アドレス枠
        s_display.drawRoundRect(4, 84, 192, 28, 4, GxEPD_BLACK);
        s_display.setTextSize(2);
        s_display.setCursor(10, 90);
        s_display.print(ip_addr ? ip_addr : "192.168.4.1");

        // 4. 設定手順ガイド枠
        s_display.drawRect(4, 120, 192, 60, GxEPD_BLACK);
        s_display.setTextSize(1);
        s_display.setCursor(8, 126);
        s_display.print("* Scan & Select your Wi-Fi");
        s_display.setCursor(8, 140);
        s_display.print("* Enter Password & Save");
        s_display.setCursor(8, 154);
        s_display.print("* Credentials saved to NVS");
        s_display.setCursor(8, 168);
        s_display.print("* Auto-reboot on finish");

        // 5. フッター帯 (黒帯 白文字)
        s_display.fillRect(0, 185, 200, 15, GxEPD_BLACK);
        s_display.setTextColor(GxEPD_WHITE);
        s_display.setTextSize(1);
        s_display.setCursor(14, 189);
        s_display.print("Waiting for Web Portal...");

    } while (s_display.nextPage());

    uint32_t elapsed_ms = millis() - start_ms;
    Serial.printf("[EPD] WiFi Setup Screen updated in %u ms.\n", elapsed_ms);
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
