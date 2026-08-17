/**
 * @file ota_client.h
 * @brief エッジ OTA (Over-The-Air) クライアントミドルウェア
 */

#ifndef OTA_CLIENT_H
#define OTA_CLIENT_H

#include "hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *target_version;
    const char *package_url;
    const char *expected_sha256;
    size_t size_bytes;
    bool mandatory;
} ota_package_info_t;

typedef enum {
    OTA_STATUS_SUCCESS = 0,
    OTA_STATUS_DOWNLOAD_FAILED = -1,
    OTA_STATUS_VERIFICATION_FAILED = -2,
    OTA_STATUS_FLASH_WRITE_FAILED = -3,
    OTA_STATUS_INVALID_METADATA = -4
} ota_status_t;

/**
 * @brief OTA更新プロセスを実行する (ダウンロード -> ハッシュ検証 -> Flash書き込み -> 再起動)
 */
ota_status_t ota_client_perform_update(const ota_package_info_t *info);

#ifdef __cplusplus
}
#endif

#endif // OTA_CLIENT_H
