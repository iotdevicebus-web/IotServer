/**
 * @file telemetry_serializer.h
 * @brief テレメトリ JSON シリアライザインターフェース
 */

#ifndef TELEMETRY_SERIALIZER_H
#define TELEMETRY_SERIALIZER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *device_id;
    uint32_t timestamp;
    uint32_t seq_no;
    const char *firmware_version;
    uint32_t boot_count;
    
    // Metrics
    float temperature;
    float humidity;
    float battery_voltage;
    uint32_t battery_level_pct;
    int32_t rssi;
    uint32_t interval_sec;

    
    // Status
    const char *state;
    uint32_t uptime_sec;
    uint32_t free_heap_bytes;
} telemetry_data_t;

/**
 * @brief telemetry_data_t を JSON 文字列にシリアライズする
 */
int serialize_telemetry_json(
    const telemetry_data_t *data,
    char *out_buffer,
    size_t buffer_size
);

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_SERIALIZER_H
