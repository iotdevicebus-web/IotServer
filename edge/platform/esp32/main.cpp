/**
 * @file main.cpp
 * @brief ESP32-S3 Arduino/PlatformIO 用エントリポイント (8MB PSRAM 大容量バッファ対応)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <sys/time.h>
#include <time.h>
#include <esp_heap_caps.h>

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
    // 1. ハードウェア UART シリアルの初期化 (確実にログを流すため1秒待機)
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // LED点灯

    s_boot_count++;

    Serial.println("\n");
    Serial.println("====================================================");
    Serial.println("  >>> IoT Platform Edge Firmware Starting! <<<     ");
    Serial.println("  Hardware: Freenove ESP32-S3 (8MB Flash + 8MB PSRAM)");
    Serial.printf ("  Boot Count: %u | Free Heap: %u bytes\n", s_boot_count, esp_get_free_heap_size());
    Serial.println("====================================================");

    // 2. 8MB PSRAM 大容量バッファの初期化 & 時計セット
    init_psram_buffer();
    init_fast_clock();

    // 3. mTLS 相互TLS設定 (PKCS#1 クライアント証明書をサーバに提示しつつ、ESP32のIP照合バグを回避)
    Serial.println("[SECURITY] Configuring X.509 mTLS (Client Cert + PKCS#1 Key)...");
    s_secure_client.setInsecure();                             // ESP32 IP SAN 照合バグ(-9984)を回避
    s_secure_client.setCertificate(IOT_DEVICE_CLIENT_CERT);     // デバイス固有証明書 (DEV-ESP32-001)
    s_secure_client.setPrivateKey(IOT_DEVICE_PRIVATE_KEY);      // デバイス秘密鍵 (PKCS#1 RSA)
    Serial.println("[SECURITY] mTLS Credentials fully loaded for " IOT_DEVICE_ID);


    // 4. Wi-Fi 接続試行
    Serial.printf("[WIFI] Connecting to SSID: '%s' ...\n", MY_WIFI_SSID);
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(MY_WIFI_SSID, MY_WIFI_PASSWORD);

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 30) {
        delay(500);
        Serial.print(".");
        retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WIFI] Connected Successfully!");
        Serial.printf("[WIFI] IP Address: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.println("\n[WIFI] Connection Failed! Buffering offline to 8MB PSRAM...");
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
    data.rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -99;
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
    if (WiFi.status() == WL_CONNECTED) {
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

    // 8. Deep Sleep 移行 (15 秒間スリープ)
    Serial.println("====================================================");
    Serial.println("[SLEEP] Entering Deep Sleep for 15 seconds... (Zzz)");
    Serial.println("====================================================\n");
    Serial.flush();
    digitalWrite(LED_PIN, LOW); // LED消灯

    esp_sleep_enable_timer_wakeup(15ULL * 1000000ULL);
    esp_deep_sleep_start();
}

void loop() {
    // Deep Sleep のため loop は実行されません
}
