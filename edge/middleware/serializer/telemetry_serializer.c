/**
 * @file telemetry_serializer.c
 * @brief テレメトリ JSON シリアライザ実装
 */

#include "telemetry_serializer.h"
#include <stdio.h>
#include <inttypes.h>

int serialize_telemetry_json(
    const telemetry_data_t *data,
    char *out_buffer,
    size_t buffer_size
) {
    if (!data || !out_buffer || buffer_size == 0) {
        return -1;
    }

    int written = snprintf(
        out_buffer,
        buffer_size,
        "{"
          "\"header\":{"
            "\"device_id\":\"%s\","
            "\"timestamp\":%" PRIu32 ","
            "\"seq_no\":%" PRIu32 ","
            "\"firmware_version\":\"%s\","
            "\"boot_count\":%" PRIu32
          "},"
          "\"metrics\":{"
            "\"temperature\":%.2f,"
            "\"humidity\":%.2f,"
            "\"battery_voltage\":%.2f,"
            "\"battery_level_pct\":%" PRIu32 ","
            "\"rssi\":%" PRId32
          "},"
          "\"status\":{"
            "\"state\":\"%s\","
            "\"uptime_sec\":%" PRIu32 ","
            "\"free_heap_bytes\":%" PRIu32
          "}"
        "}",
        data->device_id ? data->device_id : "UNKNOWN",
        data->timestamp,
        data->seq_no,
        data->firmware_version ? data->firmware_version : "1.0.0",
        data->boot_count,
        data->temperature,
        data->humidity,
        data->battery_voltage,
        data->battery_level_pct,
        data->rssi,
        data->state ? data->state : "NORMAL",
        data->uptime_sec,
        data->free_heap_bytes
    );

    if (written < 0 || (size_t)written >= buffer_size) {
        return -1; // バッファあふれまたはエラー
    }

    return written;
}
