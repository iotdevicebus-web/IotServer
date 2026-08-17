/**
 * @file protobuf_serializer.h
 * @brief エッジ Protocol Buffers v3 超軽量シリアライザ (nanopb互換)
 */

#ifndef PROTOBUF_SERIALIZER_H
#define PROTOBUF_SERIALIZER_H

#include "telemetry_serializer.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief telemetry_data_t を Protobuf (TelemetryRequest) バイナリにエンコードする
 * @param data テレメトリ構造体
 * @param out_buffer 出力先バイナリバッファ
 * @param buffer_size バッファ最大長
 * @return エンコード後のバイト数 (エラー時は -1)
 */
int serialize_telemetry_protobuf(
    const telemetry_data_t *data,
    uint8_t *out_buffer,
    size_t buffer_size
);

#ifdef __cplusplus
}
#endif

#endif // PROTOBUF_SERIALIZER_H
