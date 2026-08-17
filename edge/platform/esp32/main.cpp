/**
 * @file main.cpp
 * @brief ESP32-S3 Arduino/PlatformIO 用エントリポイント (QC規約準拠・完全イベント駆動・8MB PSRAM対応)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <sys/time.h>
#include <time.h>
#include <esp_heap_caps.h>
#include "AppConst.hpp"

extern "C" {
#include "osal.h"
#include "hal.h"
#include "hal_sleep.h"
#include "app_state_machine.h"
#include "telemetry_serializer.h"
#include "protobuf_serializer.h"
#include "telemetry_buffer.h"
#include "device_certs.h"
}

typedef struct {
    telemetry_data_t *items;
    size_t head;
    size_t tail;
    size_t count;
    size_t capacity;
    bool is_psram;
} psram_telemetry_buffer_t;

static psram_telemetry_buffer_t s_psram_buffer;
static WiFiClientSecure s_secure_client;
static RTC_DATA_ATTR uint32_t s_boot_count = 0;
static SemaphoreHandle_t s_wifi_event_sem = nullptr;

/**
 * @brief Wi-Fi イベントハンドラ (Callee 側での即時通知)
 */
static void on_wifi_event(WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        if (s_wifi_event_sem != nullptr) {
            xSemaphoreGive(s_wifi_event_sem);
        }
    }
}

/**
 * @brief 8MB PSRAM 大容量バッファの初期化 (Callee 側デバッグライト配置)
 */
static void init_psram_buffer() {
    memset(&s_psram_buffer, 0, sizeof(s_psram_buffer));
    
    if (psramFound() && psramInit()) {
        size_t psram_size = ESP.getPsramSize();
        size_t free_psram = ESP.getFreePsram();
        Serial.printf("[HARDWARE] >>> 8MB Octal PSRAM Detected & Active! Total: %u KB, Free: %u KB <<<\n",
            psram_size / 1024, free_psram / 1024);

        // 8MB PSRAM 上に大容量バッファ領域を確保
        s_psram_buffer.items = (telemetry_data_t *)heap_caps_malloc(
            sizeof(telemetry_data_t) * AppConst::PSRAM_BUFFER_CAPACITY, MALLOC_CAP_SPIRAM);
        
        if (s_psram_buffer.items != nullptr) {
            s_psram_buffer.capacity = AppConst::PSRAM_BUFFER_CAPACITY;
            s_psram_buffer.is_psram = true;
            Serial.printf("[BUFFER] Allocated High-Capacity Ring Buffer on 8MB PSRAM (Capacity: %zu records / ~%zu KB)\n",
                s_psram_buffer.capacity, (sizeof(telemetry_data_t) * AppConst::PSRAM_BUFFER_CAPACITY) / 1024);
            return;
        }
    }

    // PSRAM 未検出または確保失敗時のフォールバック (内部SRAM)
    Serial.printf("[BUFFER] Fallback: Allocating %zu records in Internal SRAM\n", AppConst::SRAM_BUFFER_FALLBACK);
    s_psram_buffer.items = (telemetry_data_t *)malloc(sizeof(telemetry_data_t) * AppConst::SRAM_BUFFER_FALLBACK);
    s_psram_buffer.capacity = (s_psram_buffer.items != nullptr) ? AppConst::SRAM_BUFFER_FALLBACK : 0;
    s_psram_buffer.is_psram = false;
}

static void psram_buffer_push(const telemetry_data_t *data) {
    if (s_psram_buffer.capacity == 0 || s_psram_buffer.items == nullptr) return;
    s_psram_buffer.items[s_psram_buffer.head] = *data;
    s_psram_buffer.head = (s_psram_buffer.head + 1) % s_psram_buffer.capacity;
    if (s_psram_buffer.count < s_psram_buffer.capacity) {
        s_psram_buffer.count++;
    } else {
        s_psram_buffer.tail = (s_psram_buffer.tail + 1) % s_psram_buffer.capacity;
    }
}

/**
 * @brief 爆速時刻セット (Callee 側デバッグライト配置)
 */
static void init_fast_clock() {
    struct timeval tv = { .tv_sec = AppConst::FAST_CLOCK_INIT_TIMESTAMP, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    time_t now = time(nullptr);
    Serial.printf("[CLOCK] Current System Time set to: %lu (2026-08-17 12:00:00 UTC)\n", (unsigned long)now);
}

/**
 * @brief mTLS 認証情報の設定 (Callee 側デバッグライト配置)
 */
static void configure_mtls_credentials() {
    Serial.println("[SECURITY] Configuring X.509 mTLS (Root CA + Client Cert + PKCS#1 Key)...");
    s_secure_client.setCACert(IOT_ROOT_CA_CERT);
    s_secure_client.setCertificate(IOT_DEVICE_CLIENT_CERT);
    s_secure_client.setPrivateKey(IOT_DEVICE_PRIVATE_KEY);
    Serial.println("[SECURITY] mTLS Credentials fully loaded for " IOT_DEVICE_ID);
}

/**
 * @brief イベント駆動型 Wi-Fi 接続 (ポーリング完全不使用)
 */
static bool connect_wifi_event_driven() {
    Serial.printf("[WIFI] Connecting to SSID: '%s' (Event-Driven Mode, Zero-Polling) ...\n", AppConst::WIFI_SSID);
    s_wifi_event_sem = xSemaphoreCreateBinary();
    WiFi.onEvent(on_wifi_event);
    WiFi.mode(WIFI_STA);
    WiFi.begin(AppConst::WIFI_SSID, AppConst::WIFI_PASSWORD);

    // ハードウェアイベント発生まで FreeRTOS セマフォでブロック待機
    bool connected = (xSemaphoreTake(s_wifi_event_sem, pdMS_TO_TICKS(AppConst::WIFI_TIMEOUT_MS)) == pdTRUE);

    if (connected && WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] Connected Event Received! (Zero-Polling Instant Wake)");
        Serial.printf("[WIFI] IP Address: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    } else {
        Serial.println("[WIFI] Connection Timeout or Failed! Buffering offline to 8MB PSRAM...");
        return false;
    }
}

/**
 * @brief テレメトリの mTLS HTTPS 送信 (Callee 側デバッグライト配置)
 */
static void send_telemetry_payload(const telemetry_data_t *data, bool is_connected) {
    uint8_t pb_buf[256];
    int pb_len = serialize_telemetry_protobuf(data, pb_buf, sizeof(pb_buf));
    Serial.printf("[PROTOBUF] Serialized payload size: %d bytes (vs JSON ~290B)\n", pb_len);

    if (is_connected && WiFi.status() == WL_CONNECTED) {
        HTTPClient https;
        String url = String("https://") + AppConst::SERVER_HOST + ":" + AppConst::SERVER_PORT + "/api/v1/telemetry";
        Serial.printf("[HTTPS] Connecting to %s ...\n", url.c_str());

        if (https.begin(s_secure_client, url)) {
            https.addHeader("Content-Type", "application/x-protobuf");
            int httpCode = https.POST(pb_buf, pb_len);

            if (httpCode > 0) {
                Serial.printf("[HTTPS] >>> POST Success! Response Code: %d <<<\n", httpCode);
                String resp = https.getString();
                Serial.println("[HTTPS] Server Response Body:\n  " + resp);
            } else {
                Serial.printf("[HTTPS] POST Failed, Error: %s\n", https.errorToString(httpCode).c_str());
                psram_buffer_push(data);
                Serial.printf("[BUFFER] Stored in 8MB PSRAM. Total buffered: %zu records\n", s_psram_buffer.count);
            }
            https.end();
        }
    } else {
        psram_buffer_push(data);
        Serial.printf("[BUFFER] Stored in 8MB PSRAM. Total buffered: %zu / %zu records\n", 
            s_psram_buffer.count, s_psram_buffer.capacity);
    }
}

/**
 * @brief Deep Sleep 移行と割り込み再有効化 (Callee 側デバッグライト配置)
 */
static void enter_power_saving_sleep() {
    Serial.println("====================================================");
    Serial.println("[SLEEP] Enabling Wakeup Sources (Interrupts Re-Enabled via HAL):");
    Serial.printf ("  1. Timer Wakeup: %u seconds\n", AppConst::DEEP_SLEEP_DURATION_SEC);
    Serial.printf ("  2. External GPIO Wakeup: GPIO %u (Active LOW)\n", AppConst::PIN_WAKEUP_BUTTON);
    Serial.println("[SLEEP] Entering Deep Sleep... (Zzz)");
    Serial.println("====================================================\n");
    Serial.flush();
    digitalWrite(AppConst::PIN_STATUS_LED, LOW); // LED消灯

    // 【全処理完了】外部割り込み & タイマー起床を有効化してスリープ
    hal_sleep_enable_gpio_wakeup((hal_gpio_pin_t)AppConst::PIN_WAKEUP_BUTTON, HAL_GPIO_INTR_LOW_LEVEL);
    hal_sleep_enable_timer_wakeup(AppConst::DEEP_SLEEP_DURATION_SEC);
    hal_sleep_enter(HAL_SLEEP_MODE_DEEP);
}

void setup() {
    // 1. ハードウェア UART シリアル初期化 & ステータス LED 点灯
    Serial.begin(AppConst::SERIAL_BAUDRATE);
    pinMode(AppConst::PIN_STATUS_LED, OUTPUT);
    digitalWrite(AppConst::PIN_STATUS_LED, HIGH);

    s_boot_count++;

    Serial.println("\n");
    // 【割り込み制御】起床検出直後に GPIO 4 の割り込みを完全禁止（HAL経由）
    hal_sleep_disable_gpio_interrupt((hal_gpio_pin_t)AppConst::PIN_WAKEUP_BUTTON);
    Serial.printf("[INTERRUPT] GPIO %u Interrupt DISABLED during active processing.\n", AppConst::PIN_WAKEUP_BUTTON);

    // 起床理由 (Wakeup Cause) の即時判定
    hal_wakeup_cause_t wakeup_cause = hal_sleep_get_wakeup_cause();
    switch (wakeup_cause) {
        case HAL_WAKEUP_CAUSE_GPIO:
            Serial.printf("[WAKEUP] 🔔 >>> External GPIO %u Interrupt Wakeup Triggered! <<<\n", AppConst::PIN_WAKEUP_BUTTON);
            break;
        case HAL_WAKEUP_CAUSE_TIMER:
            Serial.printf("[WAKEUP] ⏰ Periodic Timer Wakeup (%u s elapsed)\n", AppConst::DEEP_SLEEP_DURATION_SEC);
            break;
        default:
            Serial.printf("[WAKEUP] ⚡ Initial Power-On Reset (Cause: %d)\n", wakeup_cause);
            break;
    }
    Serial.println("====================================================");

    // 2. 8MB PSRAM 大容量バッファ & 0ms 時計初期化
    init_psram_buffer();
    init_fast_clock();

    // 3. mTLS 相互TLS証明書の設定
    configure_mtls_credentials();

    // 4. イベント駆動型 Wi-Fi 接続 (ゼロポーリング)
    bool connected = connect_wifi_event_driven();

    // 5. テレメトリデータの作成
    telemetry_data_t data;
    memset(&data, 0, sizeof(data));
    data.device_id = IOT_DEVICE_ID;
    data.timestamp = (uint32_t)time(nullptr);
    data.seq_no = s_boot_count;
    data.firmware_version = "1.0.0-PSRAM";
    data.boot_count = s_boot_count;
    data.temperature = 25.4f + (float)random(-15, 15) / 10.0f;
    data.humidity = 58.0f + (float)random(-25, 25) / 10.0f;
    data.battery_voltage = 4.05f - ((float)(s_boot_count % 10) * 0.02f);
    data.battery_level_pct = 95 - (s_boot_count % 10);
    data.rssi = (connected && WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -99;
    data.state = "NORMAL";
    data.uptime_sec = (uint32_t)(millis() / 1000);
    data.free_heap_bytes = esp_get_free_heap_size();

    Serial.printf("[SENSOR] Temp: %.2f C | Humi: %.2f %% | Batt: %.2f V (%u %%)\n",
        data.temperature, data.humidity, data.battery_voltage, data.battery_level_pct);

    // 6. Protobuf シリアライズ & mTLS HTTPS 送信
    send_telemetry_payload(&data, connected);

    // 7. Deep Sleep 移行 (全処理完了により割り込み再有効化)
    enter_power_saving_sleep();
}

void loop() {
    // Deep Sleep のため loop は実行されません
}

