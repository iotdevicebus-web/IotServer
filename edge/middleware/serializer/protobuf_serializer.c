/**
 * @file protobuf_serializer.c
 * @brief エッジ Protocol Buffers v3 超軽量シリアライザ実装
 */

#include "protobuf_serializer.h"
#include <string.h>

#define WIRE_VARINT           0
#define WIRE_FIXED32          5
#define WIRE_LENGTH_DELIMITED 2

static size_t encode_varint(uint64_t value, uint8_t *out) {
    size_t i = 0;
    while (value >= 0x80) {
        out[i++] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
    }
    out[i++] = (uint8_t)(value & 0x7F);
    return i;
}

static size_t encode_tag(uint32_t field_num, uint8_t wire_type, uint8_t *out) {
    return encode_varint(((uint64_t)field_num << 3) | (uint64_t)wire_type, out);
}

static size_t encode_fixed32_float(float val, uint8_t *out) {
    uint32_t raw;
    memcpy(&raw, &val, 4);
    out[0] = (uint8_t)(raw & 0xFF);
    out[1] = (uint8_t)((raw >> 8) & 0xFF);
    out[2] = (uint8_t)((raw >> 16) & 0xFF);
    out[3] = (uint8_t)((raw >> 24) & 0xFF);
    return 4;
}

static size_t encode_string_field(uint32_t field_num, const char *str, uint8_t *out) {
    if (!str) return 0;
    size_t len = strlen(str);
    size_t offset = encode_tag(field_num, WIRE_LENGTH_DELIMITED, out);
    offset += encode_varint((uint64_t)len, out + offset);
    memcpy(out + offset, str, len);
    return offset + len;
}

static size_t encode_varint_field(uint32_t field_num, uint64_t val, uint8_t *out) {
    size_t offset = encode_tag(field_num, WIRE_VARINT, out);
    offset += encode_varint(val, out + offset);
    return offset;
}

static size_t encode_float_field(uint32_t field_num, float val, uint8_t *out) {
    size_t offset = encode_tag(field_num, WIRE_FIXED32, out);
    offset += encode_fixed32_float(val, out + offset);
    return offset;
}

int serialize_telemetry_protobuf(
    const telemetry_data_t *data,
    uint8_t *out_buffer,
    size_t buffer_size
) {
    if (!data || !out_buffer || buffer_size < 128) {
        return -1;
    }

    // 1. MessageHeader (Sub-message)

    uint8_t header_buf[64];
    size_t h_len = 0;
    h_len += encode_string_field(1, data->device_id ? data->device_id : "UNKNOWN", header_buf + h_len);
    h_len += encode_varint_field(2, (uint64_t)data->timestamp, header_buf + h_len);
    h_len += encode_varint_field(3, (uint64_t)data->seq_no, header_buf + h_len);
    h_len += encode_string_field(4, data->firmware_version ? data->firmware_version : "1.0.0", header_buf + h_len);
    h_len += encode_varint_field(5, (uint64_t)data->boot_count, header_buf + h_len);

    // 2. MetricsData (Sub-message)
    uint8_t metrics_buf[64];
    size_t m_len = 0;
    m_len += encode_float_field(1, data->temperature, metrics_buf + m_len);
    m_len += encode_float_field(2, data->humidity, metrics_buf + m_len);
    m_len += encode_float_field(3, data->battery_voltage, metrics_buf + m_len);
    m_len += encode_varint_field(4, (uint64_t)data->battery_level_pct, metrics_buf + m_len);
    // Zigzag/Varint for int32 RSSI
    uint64_t rssi_encoded = (uint64_t)(int64_t)data->rssi;
    m_len += encode_varint_field(5, rssi_encoded, metrics_buf + m_len);
    m_len += encode_varint_field(6, (uint64_t)data->interval_sec, metrics_buf + m_len);


    // 3. DeviceStatus (Sub-message)
    uint8_t status_buf[32];
    size_t s_len = 0;
    uint32_t state_enum = 0; // NORMAL
    if (data->state && strcmp(data->state, "LOW_POWER") == 0) state_enum = 1;
    else if (data->state && strcmp(data->state, "WARNING") == 0) state_enum = 2;
    else if (data->state && strcmp(data->state, "ERROR") == 0) state_enum = 3;
    
    s_len += encode_varint_field(1, state_enum, status_buf + s_len);
    s_len += encode_varint_field(2, (uint64_t)data->uptime_sec, status_buf + s_len);
    s_len += encode_varint_field(3, (uint64_t)data->free_heap_bytes, status_buf + s_len);

    // ルートメッセージ (TelemetryRequest) の組み立て
    size_t total = 0;

    // Field 1: header
    total += encode_tag(1, WIRE_LENGTH_DELIMITED, out_buffer + total);
    total += encode_varint((uint64_t)h_len, out_buffer + total);
    memcpy(out_buffer + total, header_buf, h_len);
    total += h_len;

    // Field 2: metrics
    total += encode_tag(2, WIRE_LENGTH_DELIMITED, out_buffer + total);
    total += encode_varint((uint64_t)m_len, out_buffer + total);
    memcpy(out_buffer + total, metrics_buf, m_len);
    total += m_len;

    // Field 3: status
    total += encode_tag(3, WIRE_LENGTH_DELIMITED, out_buffer + total);
    total += encode_varint((uint64_t)s_len, out_buffer + total);
    memcpy(out_buffer + total, status_buf, s_len);
    total += s_len;

    return (int)total;
}
