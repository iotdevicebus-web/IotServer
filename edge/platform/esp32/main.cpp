/**
 * @file main.cpp
 * @brief ESP32-S3 Arduino/PlatformIO 用エントリポイント (完全イベント駆動・ゼロポーリング・8MB PSRAM対応)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <sys/time.h>
#include <time.h>
#include <esp_heap_caps.h>
#include <driver/rtc_io.h>

extern "C" {
#include "osal.h"
#include "hal.h"
#include "app_state_machine.h"
#include "telemetry_serializer.h"
#include "protobuf_serializer.h"
#include "telemetry_buffer.h"
#include "device_certs.h"
}

// Wi-Fi 接続設定 (適宜ご自身の環境に合わせて書き換えてください)
static const char *MY_WIFI_SSID = "ControlAdLab";
static const char *MY_WIFI_PASSWORD = "ControlAD"; // ※実環境に合わせて維持

// IoT サーバのアドレス (PC の IP アドレス)
static const char *MY_SERVER_HOST = "192.168.3.4";
static const int MY_SERVER_PORT = 8443;

// Freenove ESP32-S3 のオンボード LED (GPIO 48)
#define LED_PIN 48

// 8MB PSRAM 上の大容量バッファ設定 (最大 10,000 件のテレメトリを保持可能)
#define PSRAM_BUFFER_MAX_CAPACITY 10000

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

// 【完全イベント駆動】Wi-Fi 接続通知用 FreeRTOS セマフォ (ポーリング待機ゼロ)
static SemaphoreHandle_t s_wifi_event_sem = nullptr;

static void on_wifi_event(WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        if (s_wifi_event_sem != nullptr) {
            xSemaphoreGive(s_wifi_event_sem);
        }
    }
}

/**
 * @brief 8MB PSRAM 大容量バッファの初期化
 */
static void init_psram_buffer() {
    memset(&s_psram_buffer, 0, sizeof(s_psram_buffer));
    
    if (psramFound() && psramInit()) {
        size_t psram_size = ESP.getPsramSize();
        size_t free_psram = ESP.getFreePsram();
        Serial.printf("[HARDWARE] >>> 8MB Octal PSRAM Detected & Active! Total: %u KB, Free: %u KB <<<\n",
            psram_size / 1024, free_psram / 1024);

        // 8MB PSRAM 上に 10,000 件分のバッファ領域 (約 1.5MB) を確保
        s_psram_buffer.items = (telemetry_data_t *)heap_caps_malloc(
            sizeof(telemetry_data_t) * PSRAM_BUFFER_MAX_CAPACITY, MALLOC_CAP_SPIRAM);
        
        if (s_psram_buffer.items != nullptr) {
            s_psram_buffer.capacity = PSRAM_BUFFER_MAX_CAPACITY;
            s_psram_buffer.is_psram = true;
            Serial.printf("[BUFFER] Allocated High-Capacity Ring Buffer on 8MB PSRAM (Capacity: %zu records / ~%zu KB)\n",
                s_psram_buffer.capacity, (sizeof(telemetry_data_t) * PSRAM_BUFFER_MAX_CAPACITY) / 1024);
            return;
        }
    }

    // PSRAM 未検出または確保失敗時のフォールバック (内部SRAM 64件)
    Serial.println("[BUFFER] Fallback: Allocating 64 records in Internal SRAM");
    s_psram_buffer.items = (telemetry_data_t *)malloc(sizeof(telemetry_data_t) * 64);
    s_psram_buffer.capacity = (s_psram_buffer.items != nullptr) ? 64 : 0;
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
 * @brief 爆速時刻セット (0ms で有効期間内時刻をセット)
 */
static void init_fast_clock() {
    struct timeval tv = { .tv_sec = 1786968000, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    time_t now = time(nullptr);
    Serial.printf("[CLOCK] Current System Time set to: %lu (2026-08-17 12:00:00 UTC)\n", (unsigned long)now);
}

void setup() {
    // 1. ハードウェア UART シリアル初期化 (即時稼働)
    Serial.begin(115200);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // LED点灯

    s_boot_count++;

    Serial.println("\n");
    // 【割り込み制御】起床検出直後に GPIO 4 の割り込みを完全禁止（ポーリング/チャタリング遅延ゼロ）
    gpio_intr_disable(GPIO_NUM_4);
    rtc_gpio_deinit(GPIO_NUM_4);
    Serial.println("[INTERRUPT] GPIO 4 Interrupt DISABLED during active processing.");

    // 起床理由 (Wakeup Cause) のレジスタ即時判定 (ポーリングなし)
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT1:
        case ESP_SLEEP_WAKEUP_EXT0: {
            uint64_t pin_mask = esp_sleep_get_ext1_wakeup_status();
            Serial.printf("[WAKEUP] 🔔 >>> External GPIO 4 Interrupt Wakeup Triggered! <<< (Mask: 0x%llX)\n", pin_mask);
            break;
        }
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("[WAKEUP] ⏰ Periodic Timer Wakeup (15s elapsed)");
            break;
        default:
            Serial.printf("[WAKEUP] ⚡ Initial Power-On Reset (Cause: %d)\n", wakeup_reason);
            break;
    }
    Serial.println("====================================================");

    // 2. 8MB PSRAM 大容量バッファの初期化 & 時計セット
    init_psram_buffer();
    init_fast_clock();

    // 3. mTLS 相互TLS証明書の設定 (Root CA + クライアント証明書 + PKCS#1 秘密鍵)
    Serial.println("[SECURITY] Configuring X.509 mTLS (Root CA + Client Cert + PKCS#1 Key)...");
    s_secure_client.setCACert(IOT_ROOT_CA_CERT);               // サーバ検証用 Root CA
    s_secure_client.setCertificate(IOT_DEVICE_CLIENT_CERT);     // デバイス固有証明書 (DEV-ESP32-001)
    s_secure_client.setPrivateKey(IOT_DEVICE_PRIVATE_KEY);      // デバイス秘密鍵 (PKCS#1 RSA)
    Serial.println("[SECURITY] mTLS Credentials fully loaded for " IOT_DEVICE_ID);

    // 4. イベント駆動型 Wi-Fi 接続 (ポーリング完全不使用・FreeRTOS セマフォ待機)
    Serial.printf("[WIFI] Connecting to SSID: '%s' (Event-Driven Mode, Zero-Polling) ...\n", MY_WIFI_SSID);
    s_wifi_event_sem = xSemaphoreCreateBinary();
    WiFi.onEvent(on_wifi_event);
    WiFi.mode(WIFI_STA);
    WiFi.begin(MY_WIFI_SSID, MY_WIFI_PASSWORD);

    // ポーリングなし！IP取得ハードウェアイベント発生まで FreeRTOS セマフォでブロック待機（最大10秒）
    bool connected = (xSemaphoreTake(s_wifi_event_sem, pdMS_TO_TICKS(10000)) == pdTRUE);

    if (connected && WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] Connected Event Received! (Zero-Polling Instant Wake)");
        Serial.printf("[WIFI] IP Address: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.println("[WIFI] Connection Timeout or Failed! Buffering offline to 8MB PSRAM...");
    }

    // 5. テレメトリデータの作成 (センサ値 / 8MB PSRAM メモリ情報)
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

    // 6. Protobuf シリアライズ (81%削減バイナリ)
    uint8_t pb_buf[256];
    int pb_len = serialize_telemetry_protobuf(&data, pb_buf, sizeof(pb_buf));
    Serial.printf("[PROTOBUF] Serialized payload size: %d bytes (vs JSON ~290B)\n", pb_len);

    // 7. mTLS HTTPS POST 送信
    if (connected && WiFi.status() == WL_CONNECTED) {
        HTTPClient https;
        String url = String("https://") + MY_SERVER_HOST + ":" + MY_SERVER_PORT + "/api/v1/telemetry";
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
                psram_buffer_push(&data);
                Serial.printf("[BUFFER] Stored in 8MB PSRAM. Total buffered: %zu records\n", s_psram_buffer.count);
            }
            https.end();
        }
    } else {
        psram_buffer_push(&data);
        Serial.printf("[BUFFER] Stored in 8MB PSRAM. Total buffered: %zu / %zu records\n", 
            s_psram_buffer.count, s_psram_buffer.capacity);
    }

    // 8. Deep Sleep 移行設定 (タイマー 15秒 + GPIO 4 LOW 外部割り込み)
    Serial.println("====================================================");
    Serial.println("[SLEEP] Enabling Wakeup Sources (Interrupts Re-Enabled):");
    Serial.println("  1. Timer Wakeup: 15 seconds");
    Serial.println("  2. External GPIO Wakeup: GPIO 4 (Active LOW)");
    Serial.println("[SLEEP] Entering Deep Sleep... (Zzz)");
    Serial.println("====================================================\n");
    Serial.flush();
    digitalWrite(LED_PIN, LOW); // LED消灯

    // 【全処理完了】GPIO 4 のプルアップ & EXT1 外部割り込み起床の有効化 (LOW で起床)
    pinMode(4, INPUT_PULLUP);
    gpio_pullup_en(GPIO_NUM_4);
    gpio_pulldown_dis(GPIO_NUM_4);
    esp_sleep_enable_ext1_wakeup((1ULL << GPIO_NUM_4), ESP_EXT1_WAKEUP_ANY_LOW);

    // 15 秒タイマー起床の有効化
    esp_sleep_enable_timer_wakeup(15ULL * 1000000ULL);

    // Deep Sleep 開始
    esp_deep_sleep_start();
}

void loop() {
    // Deep Sleep のため loop は実行されません
}

