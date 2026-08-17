/**
 * @file telemetry_buffer.h
 * @brief エッジ側オフラインテレメトリ蓄積 & 再送バッファ
 */

#ifndef TELEMETRY_BUFFER_H
#define TELEMETRY_BUFFER_H

#include "telemetry_serializer.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_BUFFER_CAPACITY 32

typedef struct {
    telemetry_data_t items[TELEMETRY_BUFFER_CAPACITY];
    char device_id_storage[TELEMETRY_BUFFER_CAPACITY][32];
    char fw_version_storage[TELEMETRY_BUFFER_CAPACITY][16];
    char state_storage[TELEMETRY_BUFFER_CAPACITY][16];
    size_t head;
    size_t tail;
    size_t count;
} telemetry_ring_buffer_t;

/**
 * @brief バッファの初期化
 */
void telemetry_buffer_init(telemetry_ring_buffer_t *buf);

/**
 * @brief 未送信テレメトリをバッファにプッシュ蓄積する
 * @return 成功時は true (満杯時は最古データを上書きして true)
 */
bool telemetry_buffer_push(telemetry_ring_buffer_t *buf, const telemetry_data_t *data);

/**
 * @brief バッファ先頭の未送信テレメトリを参照する (削除はしない)
 */
bool telemetry_buffer_peek(const telemetry_ring_buffer_t *buf, telemetry_data_t *out_data);

/**
 * @brief 送信成功した先頭テレメトリをコミット破棄する
 */
bool telemetry_buffer_pop(telemetry_ring_buffer_t *buf);

/**
 * @brief 現在蓄積されている未送信件数を取得する
 */
size_t telemetry_buffer_count(const telemetry_ring_buffer_t *buf);

/**
 * @brief バッファが空かどうかを判定する
 */
bool telemetry_buffer_is_empty(const telemetry_ring_buffer_t *buf);

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_BUFFER_H
