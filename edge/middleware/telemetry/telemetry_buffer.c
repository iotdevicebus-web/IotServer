/**
 * @file telemetry_buffer.c
 * @brief エッジ側オフラインテレメトリ蓄積 & 再送バッファ実装
 */

#include "telemetry_buffer.h"
#include <string.h>

void telemetry_buffer_init(telemetry_ring_buffer_t *buf) {
    if (!buf) return;
    memset(buf, 0, sizeof(telemetry_ring_buffer_t));
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
}

bool telemetry_buffer_push(telemetry_ring_buffer_t *buf, const telemetry_data_t *data) {
    if (!buf || !data) return false;

    // バッファ満杯時は最も古いデータ (head) を進めて上書き
    if (buf->count == TELEMETRY_BUFFER_CAPACITY) {
        buf->head = (buf->head + 1) % TELEMETRY_BUFFER_CAPACITY;
        buf->count--;
    }

    size_t idx = buf->tail;
    buf->items[idx] = *data;

    // 文字列のディープコピー
    if (data->device_id) {
        strncpy(buf->device_id_storage[idx], data->device_id, sizeof(buf->device_id_storage[idx]) - 1);
        buf->items[idx].device_id = buf->device_id_storage[idx];
    }
    if (data->firmware_version) {
        strncpy(buf->fw_version_storage[idx], data->firmware_version, sizeof(buf->fw_version_storage[idx]) - 1);
        buf->items[idx].firmware_version = buf->fw_version_storage[idx];
    }
    if (data->state) {
        strncpy(buf->state_storage[idx], data->state, sizeof(buf->state_storage[idx]) - 1);
        buf->items[idx].state = buf->state_storage[idx];
    }

    buf->tail = (buf->tail + 1) % TELEMETRY_BUFFER_CAPACITY;
    buf->count++;
    return true;
}

bool telemetry_buffer_peek(const telemetry_ring_buffer_t *buf, telemetry_data_t *out_data) {
    if (!buf || !out_data || buf->count == 0) return false;
    *out_data = buf->items[buf->head];
    return true;
}

bool telemetry_buffer_pop(telemetry_ring_buffer_t *buf) {
    if (!buf || buf->count == 0) return false;
    buf->head = (buf->head + 1) % TELEMETRY_BUFFER_CAPACITY;
    buf->count--;
    return true;
}

size_t telemetry_buffer_count(const telemetry_ring_buffer_t *buf) {
    return buf ? buf->count : 0;
}

bool telemetry_buffer_is_empty(const telemetry_ring_buffer_t *buf) {
    return !buf || buf->count == 0;
}
