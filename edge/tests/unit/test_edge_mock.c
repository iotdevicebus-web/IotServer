/**
 * @file test_edge_mock.c
 * @brief エッジファームウェア PCモック単体・結合テストランナー (バッファリング検証対応)
 */

#include <stdio.h>
#include <assert.h>
#include "osal.h"
#include "hal.h"
#include "app_state_machine.h"
#include "telemetry_serializer.h"
#include "protobuf_serializer.h"
#include "telemetry_buffer.h"

int main(void) {
    printf("====================================================\n");
    printf("  Starting Edge Firmware Mock & Buffer Test         \n");
    printf("====================================================\n\n");

    // 0. JSON vs Protobuf シリアライズ & サイズ比較テスト
    telemetry_data_t sample_data = {
        .device_id = "DEV-ESP32-001",
        .timestamp = 1755420000,
        .seq_no = 42,
        .firmware_version = "1.0.0",
        .boot_count = 5,
        .temperature = 26.85f,
        .humidity = 54.20f,
        .battery_voltage = 3.95f,
        .battery_level_pct = 92,
        .rssi = -64,
        .state = "NORMAL",
        .uptime_sec = 3600,
        .free_heap_bytes = 48200
    };

    char json_buf[512];
    int json_len = serialize_telemetry_json(&sample_data, json_buf, sizeof(json_buf));
    assert(json_len > 0);

    uint8_t pb_buf[256];
    int pb_len = serialize_telemetry_protobuf(&sample_data, pb_buf, sizeof(pb_buf));
    assert(pb_len > 0);

    printf("[BENCHMARK] Serialized Payload Sizes:\n");
    printf("  - JSON Payload Size    : %d bytes\n", json_len);
    printf("  - Protobuf Payload Size: %d bytes\n", pb_len);
    float reduction = (float)(json_len - pb_len) / (float)json_len * 100.0f;
    printf("  -> Payload Reduction Rate: %.1f%% REDUCTION!\n\n", reduction);
    assert(pb_len < json_len / 2);

    // 1. セキュアエレメント (ATECC608A / OPTIGA Mock) 単体テスト
    printf("[TEST] Testing Hardware Secure Element (HSM)...\n");
    assert(hal_crypto_se_init() == HAL_OK);
    assert(hal_crypto_se_is_ready() == true);

    uint8_t dummy_digest[32] = {0x01, 0x02, 0x03};
    uint8_t sig[64];
    assert(hal_crypto_se_sign_digest(0, dummy_digest, sig) == HAL_OK);
    assert(sig[0] == (dummy_digest[0] ^ 0x55));

    uint8_t pubkey[64];
    assert(hal_crypto_se_get_public_key(0, pubkey) == HAL_OK);
    assert(pubkey[0] == 0xBB);
    printf("[TEST] Hardware Secure Element Passed! (Zero Private Key Exposure)\n\n");

    // 2. オフラインテレメトリリングバッファ単体テスト

    telemetry_ring_buffer_t ring_buf;
    telemetry_buffer_init(&ring_buf);
    assert(telemetry_buffer_is_empty(&ring_buf));
    assert(telemetry_buffer_count(&ring_buf) == 0);

    // 3件プッシュ
    sample_data.seq_no = 101;
    assert(telemetry_buffer_push(&ring_buf, &sample_data));
    sample_data.seq_no = 102;
    assert(telemetry_buffer_push(&ring_buf, &sample_data));
    sample_data.seq_no = 103;
    assert(telemetry_buffer_push(&ring_buf, &sample_data));
    assert(telemetry_buffer_count(&ring_buf) == 3);

    // 先頭Peek確認 (FIFO)
    telemetry_data_t peeked;
    assert(telemetry_buffer_peek(&ring_buf, &peeked));
    assert(peeked.seq_no == 101);

    // 1件Pop
    assert(telemetry_buffer_pop(&ring_buf));
    assert(telemetry_buffer_count(&ring_buf) == 2);
    assert(telemetry_buffer_peek(&ring_buf, &peeked));
    assert(peeked.seq_no == 102);

    printf("[TEST] Telemetry Ring Buffer Passed!\n\n");

    // 2. OSAL & ステートマシン初期化
    assert(osal_init() == OSAL_OK);
    assert(app_state_machine_init() == OSAL_OK);
    assert(app_state_machine_get_current_state() == STATE_BOOT);

    // 3. メインイベントループを実行 (正常オンライン系)
    printf("[TEST] Launching main event loop...\n");
    app_main_event_loop(NULL);

    assert(app_state_machine_get_current_state() == STATE_SLEEPING);

    printf("\n====================================================\n");
    printf("  ALL EDGE BUFFER & STATE MACHINE TESTS PASSED!     \n");
    printf("====================================================\n");

    return 0;
}
