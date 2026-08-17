/**
 * @file main.cpp
 * @brief ESP32-S3 Arduino/PlatformIO 用エントリポイント (Freenove ESP32-S3 WROOM 向け)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

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
static const char *MY_WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"; // ※実環境に合わせて維持

// IoT サーバのアドレス (PC の IP アドレス)
static const char *MY_SERVER_HOST = "192.168.3.4";
static const int MY_SERVER_PORT = 8443;

// Freenove ESP32-S3 のオンボード LED (GPIO 48)
#define LED_PIN 48

static telemetry_ring_buffer_t s_buffer;
static WiFiClientSecure s_secure_client;
static RTC_DATA_ATTR uint32_t s_boot_count = 0;

/**
 * @brief 爆速 NTP 時刻同期 (DNS解決スキップ・IP直指定 & Deep Sleep時 0ms スキップ)
 */
static void sync_time_via_fast_ntp() {
    time_t now = time(nullptr);

    // 1. Deep Sleep からの起床時: 内部 RTC がすでに正確な時刻を保持していれば即座にスキップ (所要時間: 0ms)
    if (now >= 1700000000) {
        Serial.printf("[NTP FAST] RTC clock already valid (%lu). Skipping NTP (0ms)!\n", (unsigned long)now);
        return;
    }

    // 2. 初回起動時 (電源投入直後): IPアドレス直接指定により DNS 名前解決（数秒の遅延）を完全スキップ
    //   - IP 1: 133.243.238.163 (NICT 日本標準時 NTP サーバー)
    //   - IP 2: 216.239.35.0    (Google Public NTP)
    //   - IP 3: 162.159.200.1   (Cloudflare NTP)
    Serial.print("[NTP FAST] Synchronizing via direct IP (DNS-free: NICT 133.243.238.163 / Google 216.239.35.0)...");
    configTime(9 * 3600, 0, "133.243.238.163", "216.239.35.0", "162.159.200.1");

    int retry = 0;
    while (now < 1700000000 && retry < 40) { // 50ms ごとにチェック (最大2秒)
        delay(50);
        now = time(nullptr);
        retry++;
    }

    if (now >= 1700000000) {
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        Serial.printf(" Done in %d ms!\n[NTP FAST] Synchronized UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
            retry * 50,
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.println("\n[NTP FAST] Warning: Fast NTP timeout, fallback to estimated clock");
    }
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
    Serial.println("  Hardware: Freenove ESP32-S3 WROOM (Fast NTP)      ");
    Serial.printf ("  Boot Count: %u | Free Heap: %u bytes\n", s_boot_count, esp_get_free_heap_size());
    Serial.println("====================================================");

    // 2. バッファ初期化
    telemetry_buffer_init(&s_buffer);

    // 3. mTLS 証明書の設定 (device_certs.h からロード)
    Serial.println("[SECURITY] Configuring X.509 mTLS Certificates...");
    s_secure_client.setCACert(IOT_ROOT_CA_CERT);
    s_secure_client.setCertificate(IOT_DEVICE_CLIENT_CERT);
    s_secure_client.setPrivateKey(IOT_DEVICE_PRIVATE_KEY);

    // 4. Wi-Fi 接続試行
    Serial.printf("[WIFI] Connecting to SSID: '%s' ...\n", MY_WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(MY_WIFI_SSID, MY_WIFI_PASSWORD);

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
        delay(200);
        Serial.print(".");
        retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WIFI] Connected Successfully!");
        Serial.printf("[WIFI] IP Address: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

        // 爆速 NTP 同期 (初回のみ IP 直指定で同期、2回目以降は 0ms)
        sync_time_via_fast_ntp();
    } else {
        Serial.println("\n[WIFI] Connection Failed. Buffering offline!");
    }

    // 5. テレメトリデータの作成 (ダミー / センサ値)
    telemetry_data_t data;
    memset(&data, 0, sizeof(data));
    data.device_id = IOT_DEVICE_ID;
    data.timestamp = (uint32_t)time(nullptr);
    data.seq_no = s_boot_count;
    data.firmware_version = "1.0.0";
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
                telemetry_buffer_push(&s_buffer, &data);
            }
            https.end();
        }
    } else {
        telemetry_buffer_push(&s_buffer, &data);
        Serial.printf("[BUFFER] Stored 1 record offline. Total in buffer: %zu\n", telemetry_buffer_count(&s_buffer));
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
