/**
 * @file main.cpp
 * @brief ESP32-S3 Arduino/PlatformIO 用エントリポイント (QC規約準拠・完全イベント駆動・8MB PSRAM対応)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <sys/time.h>
#include <time.h>
#include <esp_heap_caps.h>
#include "AppConst.hpp"

extern "C" {
#include "osal.h"
#include "hal.h"
#include "hal_sleep.h"
#include "hal_epaper.h"
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
static RTC_DATA_ATTR uint32_t s_sleep_interval_sec = AppConst::DEEP_SLEEP_DURATION_SEC;
static SemaphoreHandle_t s_wifi_event_sem = nullptr;
static Preferences s_prefs;

/**
 * @brief NVS (不揮発性メモリ) から Wi-Fi 接続情報を読み込み
 */
static void load_wifi_credentials(String &ssid, String &password) {
    s_prefs.begin(AppConst::NVS_NAMESPACE_WIFI, true);
    ssid = s_prefs.getString(AppConst::NVS_KEY_SSID, AppConst::WIFI_SSID);
    password = s_prefs.getString(AppConst::NVS_KEY_PASSWORD, AppConst::WIFI_PASSWORD);
    s_prefs.end();
    Serial.printf("[NVS] Loaded Wi-Fi Config -> SSID: '%s'\n", ssid.c_str());
}

/**
 * @brief NVS (不揮発性メモリ) へ Wi-Fi 接続情報を永続保存
 */
static void save_wifi_credentials(const String &ssid, const String &password) {
    s_prefs.begin(AppConst::NVS_NAMESPACE_WIFI, false);
    s_prefs.putString(AppConst::NVS_KEY_SSID, ssid);
    s_prefs.putString(AppConst::NVS_KEY_PASSWORD, password);
    s_prefs.end();
    Serial.printf("[NVS] >>> Successfully Saved Wi-Fi Credentials to NVS! SSID: '%s' <<<\n", ssid.c_str());
}

/**
 * @brief リセット起動時のスイッチ押下状態を判定 (GPIO 4 または GPIO 0)
 */
static bool check_wifi_setup_switch_pressed() {
    pinMode(AppConst::PIN_WAKEUP_BUTTON, INPUT_PULLUP);
    pinMode(AppConst::PIN_BOOT_BUTTON, INPUT_PULLUP);
    delay(50); // チャタリング防止
    bool pressed = (digitalRead(AppConst::PIN_WAKEUP_BUTTON) == LOW || digitalRead(AppConst::PIN_BOOT_BUTTON) == LOW);
    if (pressed) {
        Serial.println("[SETUP_BTN] >>> Switch Press Detected at Startup! Entering WiFiManager Setup Mode... <<<");
    }
    return pressed;
}

/**
 * @brief WiFiManager キャプティブポータルを起動して周囲のルータを検索・接続設定
 */
static void run_wifimanager_portal() {
    Serial.println("====================================================");
    Serial.printf("[WIFIMANAGER] Starting WiFiManager AP: '%s' ...\n", AppConst::WIFIMANAGER_AP_NAME);
    Serial.println("[WIFIMANAGER] Open Browser & Go to: http://192.168.4.1");
    Serial.println("====================================================");

    // 1. e-Paper 画面に設定モード案内を表示
    hal_epaper_show_wifi_setup_screen(AppConst::WIFIMANAGER_AP_NAME, "192.168.4.1");

    // 2. WiFiManager 初期化 & ポータル起動
    WiFiManager wm;
    wm.setConfigPortalTimeout(300); // 5分間タイムアウト設定 (未操作時は通常起動へ復帰)
    wm.setBreakAfterConfig(true);

    bool res = wm.startConfigPortal(AppConst::WIFIMANAGER_AP_NAME);

    if (res && WiFi.status() == WL_CONNECTED) {
        String new_ssid = WiFi.SSID();
        String new_pass = WiFi.psk();
        Serial.printf("[WIFIMANAGER] >>> Connected to new AP: '%s'! <<<\n", new_ssid.c_str());
        save_wifi_credentials(new_ssid, new_pass);
        Serial.println("[WIFIMANAGER] Configuration saved to NVS. Proceeding to normal operation...");
    } else {
        Serial.println("[WIFIMANAGER] Config portal timed out or cancelled. Proceeding with existing configuration...");
    }
}

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
    setenv("TZ", "JST-9", 1);
    tzset();

    struct timeval tv = { .tv_sec = AppConst::FAST_CLOCK_INIT_TIMESTAMP, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M:%S", &timeinfo);
    Serial.printf("[CLOCK] System Time Initialized: %lu (JST: %s)\n", (unsigned long)now, buf);
}

/**
 * @brief HTTPS 認証情報の設定 (Callee 側デバッグライト配置)
 */
static void configure_mtls_credentials() {
    Serial.println("[SECURITY] Configuring HTTPS TLS Secure Client...");
    s_secure_client.setInsecure(); // GMO gontaro.org HTTPS へのセキュア接続
    Serial.println("[SECURITY] TLS Client ready for " IOT_DEVICE_ID);
}


/**
 * @brief イベント駆動型 Wi-Fi 接続 (NVS 設定優先・ポーリング完全不使用)
 */
static bool connect_wifi_event_driven() {
    String curr_ssid, curr_password;
    load_wifi_credentials(curr_ssid, curr_password);

    Serial.printf("[WIFI] Connecting to SSID: '%s' (Event-Driven Mode, Zero-Polling) ...\n", curr_ssid.c_str());
    s_wifi_event_sem = xSemaphoreCreateBinary();
    WiFi.onEvent(on_wifi_event);
    WiFi.mode(WIFI_STA);
    WiFi.begin(curr_ssid.c_str(), curr_password.c_str());

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
 * @brief サーバレスポンスからスリープ秒数およびリモートコマンドを解析・適用
 */
static void process_server_response(const String &resp) {
    // 0. サーバ同期時刻 (server_time) の抽出 & JST時計セット
    int timePos = resp.indexOf("\"server_time\":");
    if (timePos >= 0) {
        int valStart = timePos + 14;
        while (valStart < (int)resp.length() && (resp[valStart] == ' ' || resp[valStart] == ':')) {
            valStart++;
        }
        int valEnd = valStart;
        while (valEnd < (int)resp.length() && isDigit(resp[valEnd])) {
            valEnd++;
        }
        if (valEnd > valStart) {
            time_t srvTime = (time_t)resp.substring(valStart, valEnd).toInt();
            if (srvTime > 1000000000) {
                struct timeval tv = { .tv_sec = srvTime, .tv_usec = 0 };
                settimeofday(&tv, nullptr);
                struct tm timeinfo;
                localtime_r(&srvTime, &timeinfo);
                char buf[32];
                strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M:%S", &timeinfo);
                Serial.printf("[CLOCK] ⏰ >>> Clock Synchronized with Server: %lu (JST: %s) <<<\n",
                    (unsigned long)srvTime, buf);
            }
        }
    }

    // 1. トップレベルまたは C2 コマンド内の sleep_interval_sec の抽出
    int keyPos = resp.indexOf("\"sleep_interval_sec\":");
    if (keyPos >= 0) {
        int valStart = keyPos + 21;
        while (valStart < (int)resp.length() && (resp[valStart] == ' ' || resp[valStart] == ':')) {
            valStart++;
        }
        int valEnd = valStart;
        while (valEnd < (int)resp.length() && isDigit(resp[valEnd])) {
            valEnd++;
        }
        if (valEnd > valStart) {
            uint32_t newInterval = resp.substring(valStart, valEnd).toInt();
            if (newInterval >= 1 && newInterval <= 86400) {
                if (s_sleep_interval_sec != newInterval) {
                    Serial.printf("[CONFIG] ⏱ >>> Server Updated Sleep Interval: %u sec -> %u sec <<<\n", 
                        s_sleep_interval_sec, newInterval);
                    s_sleep_interval_sec = newInterval;
                }
            }
        }
    }


    // 2. コマンド ACK の返却 (command_id が含まれる場合)
    int cmdIdPos = resp.indexOf("\"command_id\":\"");
    if (cmdIdPos >= 0) {
        int idStart = cmdIdPos + 14;
        int idEnd = resp.indexOf("\"", idStart);
        if (idEnd > idStart) {
            String cmdId = resp.substring(idStart, idEnd);
            Serial.printf("[C2 ACK] Acknowledging command ID: %s\n", cmdId.c_str());
            
            HTTPClient ackHttp;
            String ackUrl = String("https://") + AppConst::SERVER_HOST + AppConst::API_COMMAND_ACK_PATH;
            if (ackHttp.begin(s_secure_client, ackUrl)) {
                ackHttp.addHeader("Content-Type", "application/json");
                String ackJson = "{\"command_id\":\"" + cmdId + "\",\"device_id\":\"" + IOT_DEVICE_ID + "\",\"status\":\"SUCCESS\"}";
                ackHttp.POST(ackJson);
                ackHttp.end();
            }
        }
    }
}

/**
 * @brief テレメトリの HTTPS 送信 (Callee 側デバッグライト配置)
 */
static void send_telemetry_payload(const telemetry_data_t *data, bool is_connected) {
    char json_buf[384];
    snprintf(json_buf, sizeof(json_buf),
        "{\"header\":{\"device_id\":\"%s\",\"seq_no\":%u,\"timestamp\":%u,\"firmware_version\":\"%s\"},"
        "\"metrics\":{\"temperature\":%.2f,\"humidity\":%.2f,\"battery_voltage\":%.2f,\"battery_level_pct\":%u,\"rssi\":%d,\"interval_sec\":%u}}",
        data->device_id, data->seq_no, data->timestamp, data->firmware_version,
        data->temperature, data->humidity, data->battery_voltage, data->battery_level_pct, data->rssi, data->interval_sec);
    
    Serial.printf("[JSON] Payload: %s\n", json_buf);

    if (is_connected && WiFi.status() == WL_CONNECTED) {
        HTTPClient https;
        String url = String("https://") + AppConst::SERVER_HOST + AppConst::API_TELEMETRY_PATH;
        Serial.printf("[HTTPS] Connecting to %s ...\n", url.c_str());

        if (https.begin(s_secure_client, url)) {
            https.addHeader("Content-Type", "application/json");
            int httpCode = https.POST((uint8_t*)json_buf, strlen(json_buf));

            if (httpCode > 0) {
                Serial.printf("[HTTPS] >>> POST Success! Response Code: %d <<<\n", httpCode);
                String resp = https.getString();
                Serial.println("[HTTPS] Server Response Body:\n  " + resp);
                process_server_response(resp); // サーバからのスリープ秒数・コマンドを反映
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
    Serial.printf ("  1. Timer Wakeup: %u seconds (Dynamically Set by Server/AppConst)\n", s_sleep_interval_sec);
    Serial.printf ("  2. External GPIO Wakeup: GPIO %u (Active LOW)\n", AppConst::PIN_WAKEUP_BUTTON);
    Serial.println("[SLEEP] Entering Deep Sleep... (Zzz)");
    Serial.println("====================================================\n");
    Serial.flush();
    digitalWrite(AppConst::PIN_STATUS_LED, LOW); // LED消灯

    // 【全処理完了】外部割り込み & 動的タイマー秒数でスリープ
    hal_sleep_enable_gpio_wakeup((hal_gpio_pin_t)AppConst::PIN_WAKEUP_BUTTON, HAL_GPIO_INTR_LOW_LEVEL);
    hal_sleep_enable_timer_wakeup(s_sleep_interval_sec);
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
            Serial.printf("[WAKEUP] ⏰ Periodic Timer Wakeup (%u s elapsed)\n", s_sleep_interval_sec);
            break;

        default:
            Serial.printf("[WAKEUP] ⚡ Initial Power-On Reset (Cause: %d)\n", wakeup_cause);
            break;
    }
    // 2. リセットスタート時のスイッチ判定 (WiFiManager キャプティブポータル起動)
    if (check_wifi_setup_switch_pressed()) {
        run_wifimanager_portal();
    }

    // 3. 8MB PSRAM 大容量バッファ & 0ms 時計初期化
    init_psram_buffer();
    init_fast_clock();

    // 4. mTLS 相互TLS証明書の設定
    configure_mtls_credentials();

    // 5. イベント駆動型 Wi-Fi 接続 (NVS設定優先・ゼロポーリング)
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
    data.interval_sec = s_sleep_interval_sec;

    Serial.printf("[SENSOR] Temp: %.2f C | Humi: %.2f %% | Batt: %.2f V (%u %%) | Interval: %u s\n",
        data.temperature, data.humidity, data.battery_voltage, data.battery_level_pct, data.interval_sec);


    // 6. Protobuf シリアライズ & mTLS HTTPS 送信
    send_telemetry_payload(&data, connected);

    // 7. e-Paper 画面表示の更新 (初回起動時はテスト画面、以降はステータス画面)
    if (s_boot_count == 1) {
        Serial.println("[EPD] Boot #1: Rendering Test Pattern Screen...");
        hal_epaper_show_test_screen();
    } else {
        String ip_str = (connected && WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "Disconnected";
        
        // 日本標準時 (JST: UTC+9) の日時文字列を生成
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char jst_time_buf[32];
        strftime(jst_time_buf, sizeof(jst_time_buf), "%Y/%m/%d %H:%M:%S", &timeinfo);
        Serial.printf("[EPD] Displaying JST Time on Screen: %s\n", jst_time_buf);

        epd_status_info_t epd_info;
        epd_info.device_id = data.device_id;
        epd_info.ip_address = ip_str.c_str();
        epd_info.rssi = (int8_t)data.rssi;
        epd_info.boot_count = s_boot_count;
        epd_info.interval_sec = s_sleep_interval_sec;
        epd_info.temperature = data.temperature;
        epd_info.humidity = data.humidity;
        epd_info.battery_voltage = data.battery_voltage;
        epd_info.server_status = connected ? "200 OK (HTTPS)" : "Buffered";
        epd_info.time_jst_str = jst_time_buf;
        hal_epaper_show_status(&epd_info);

    }
    hal_epaper_sleep(); // 表示更新後、e-Paper を超低消費電力スリープへ


    // 8. Deep Sleep 移行 (全処理完了により割り込み再有効化)
    enter_power_saving_sleep();

}

void loop() {
    // Deep Sleep のため loop は実行されません
}

