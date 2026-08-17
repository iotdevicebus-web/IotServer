/**
 * @file app_state_machine.c
 * @brief エッジ省電力イベント駆動ステートマシン実装 (オフラインバッファリング対応)
 */

#include "app_state_machine.h"
#include "osal.h"
#include "hal.h"
#include "telemetry_serializer.h"
#include "protobuf_serializer.h"
#include "telemetry_buffer.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define EVENT_QUEUE_LEN     16
#define HTTP_RESP_BUF_SIZE  1024
#define JSON_PAYLOAD_SIZE   512

static app_state_t s_current_state = STATE_BOOT;
static osal_queue_handle_t s_event_queue = NULL;
static telemetry_ring_buffer_t s_offline_buffer;
static uint32_t s_seq_no = 1;
static uint32_t s_boot_count = 1;
static uint32_t s_next_sleep_sec = 30;

osal_status_t app_state_machine_init(void) {
    osal_status_t status = osal_queue_create(EVENT_QUEUE_LEN, sizeof(app_event_msg_t), &s_event_queue);
    if (status != OSAL_OK) {
        printf("[APP] Failed to create event queue!\n");
        return status;
    }

    telemetry_buffer_init(&s_offline_buffer);
    s_current_state = STATE_BOOT;
    printf("[APP] State Machine Initialized with Offline Buffer. State: STATE_BOOT\n");
    return OSAL_OK;
}

app_state_t app_state_machine_get_current_state(void) {
    return s_current_state;
}

static void transition_to(app_state_t next_state) {
    const char *state_names[] = {
        "STATE_BOOT", "STATE_SENSING", "STATE_CONNECTING",
        "STATE_TRANSMITTING", "STATE_OTA_PROCESSING",
        "STATE_PREPARE_SLEEP", "STATE_SLEEPING"
    };
    printf("[STATE TRANSITION] %s -> %s\n", state_names[s_current_state], state_names[next_state]);
    s_current_state = next_state;
}

osal_status_t app_state_machine_dispatch(const app_event_msg_t *event) {
    if (!s_event_queue || !event) return OSAL_ERR_INVALID_PARAM;
    return osal_queue_send(s_event_queue, event, 100);
}

static bool transmit_telemetry_item(const telemetry_data_t *item) {
    char payload_buf[JSON_PAYLOAD_SIZE];
    int payload_len = serialize_telemetry_json(item, payload_buf, sizeof(payload_buf));
    if (payload_len <= 0) return false;

    char resp_buf[HTTP_RESP_BUF_SIZE];
    hal_http_post_request_t req = {
        .url = "https://127.0.0.1:8443/api/v1/telemetry",
        .content_type = "application/json",
        .payload = (const uint8_t *)payload_buf,
        .payload_len = (size_t)payload_len,
        .tls_creds = NULL,
        .timeout_ms = 5000
    };
    hal_http_response_t resp = {
        .status_code = 0,
        .response_buffer = (uint8_t *)resp_buf,
        .buffer_size = sizeof(resp_buf),
        .received_len = 0
    };

    hal_status_t tx_res = hal_https_post(&req, &resp);
    if (tx_res == HAL_OK && resp.status_code == 200) {
        printf("[APP] Telemetry TX Success (Seq=%" PRIu32 ")! Server Resp: %s\n", item->seq_no, resp_buf);
        return true;
    } else {
        printf("[APP] Telemetry TX Failed (Seq=%" PRIu32 "). Code: %d\n", item->seq_no, resp.status_code);
        return false;
    }
}

void app_main_event_loop(void *arg) {
    (void)arg;
    app_event_msg_t event;

    app_event_msg_t boot_evt = {
        .type = EVENT_TYPE_WAKEUP_TIMER,
        .timestamp = osal_get_time_ms() / 1000
    };
    app_state_machine_dispatch(&boot_evt);

    while (1) {
        if (osal_queue_receive(s_event_queue, &event, OSAL_WAIT_FOREVER) != OSAL_OK) {
            continue;
        }

        printf("[APP EVENT] Received Event Type: %d at %" PRIu32 " ms\n", event.type, osal_get_time_ms());

        switch (s_current_state) {
            case STATE_BOOT: {
                hal_init();
                hal_wakeup_cause_t cause = hal_sleep_get_wakeup_cause();
                printf("[APP] Wakeup Cause: %d\n", cause);
                transition_to(STATE_SENSING);

                // センシング測定
                telemetry_data_t current_telemetry;
                memset(&current_telemetry, 0, sizeof(current_telemetry));
                current_telemetry.device_id = "DEV-ESP32-001";
                current_telemetry.timestamp = osal_get_time_ms() / 1000;
                current_telemetry.seq_no = s_seq_no++;
                current_telemetry.firmware_version = "1.0.0";
                current_telemetry.boot_count = s_boot_count;
                current_telemetry.state = "NORMAL";
                current_telemetry.uptime_sec = osal_get_time_ms() / 1000;
                current_telemetry.free_heap_bytes = 48000;

                uint32_t mv = 0;
                hal_adc_read_voltage_mv(0, &mv);
                current_telemetry.battery_voltage = (float)mv / 1000.0f;
                current_telemetry.battery_level_pct = (mv > 4000) ? 100 : (mv > 3300 ? (mv - 3300) * 100 / 700 : 0);
                current_telemetry.temperature = 25.4f;
                current_telemetry.humidity = 58.2f;

                int rssi = 0;
                hal_network_get_rssi(&rssi);
                current_telemetry.rssi = rssi;

                transition_to(STATE_CONNECTING);
                hal_status_t net_status = hal_network_connect();

                if (net_status == HAL_OK) {
                    transition_to(STATE_TRANSMITTING);

                    // 1. オフライン蓄積データのフラッシュ送信
                    size_t buffered_cnt = telemetry_buffer_count(&s_offline_buffer);
                    if (buffered_cnt > 0) {
                        printf("[APP BUFFER] Flushing %zu buffered offline telemetry records...\n", buffered_cnt);
                        while (!telemetry_buffer_is_empty(&s_offline_buffer)) {
                            telemetry_data_t old_item;
                            if (telemetry_buffer_peek(&s_offline_buffer, &old_item)) {
                                if (transmit_telemetry_item(&old_item)) {
                                    telemetry_buffer_pop(&s_offline_buffer);
                                } else {
                                    printf("[APP BUFFER] Flush interrupted due to TX failure.\n");
                                    break;
                                }
                            }
                        }
                    }

                    // 2. 今回測定したデータの送信
                    if (!transmit_telemetry_item(&current_telemetry)) {
                        // 送信失敗時はバッファへ退避
                        printf("[APP] Live TX failed, saving to offline buffer.\n");
                        telemetry_buffer_push(&s_offline_buffer, &current_telemetry);
                    }
                } else {
                    // ネットワーク接続失敗時: データをオフラインバッファに退避
                    printf("[APP] Network offline! Buffering telemetry (Seq=%" PRIu32 ") to Flash/RAM storage.\n", current_telemetry.seq_no);
                    telemetry_buffer_push(&s_offline_buffer, &current_telemetry);
                    printf("[APP] Current buffered items count: %zu\n", telemetry_buffer_count(&s_offline_buffer));
                }

                // スリープ準備へ
                transition_to(STATE_PREPARE_SLEEP);
                hal_network_disconnect();
                hal_sleep_enable_timer_wakeup(s_next_sleep_sec);
                hal_sleep_enable_gpio_wakeup(0, HAL_GPIO_INTR_FALLING);

                transition_to(STATE_SLEEPING);
                hal_sleep_enter(HAL_SLEEP_MODE_DEEP);

                return;
            }

            default:
                break;
        }
    }
}
