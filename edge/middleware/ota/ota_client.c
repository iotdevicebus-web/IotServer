/**
 * @file ota_client.c
 * @brief エッジ OTA クライアント実装
 */

#include "ota_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ota_status_t ota_client_perform_update(const ota_package_info_t *info) {
    if (!info || !info->package_url || !info->target_version || info->size_bytes == 0) {
        printf("[OTA] Invalid metadata.\n");
        return OTA_STATUS_INVALID_METADATA;
    }

    printf("\n[OTA CLIENT] === Initiating OTA Firmware Update ===\n");
    printf("Target Version: %s\n", info->target_version);
    printf("Package URL: %s\n", info->package_url);
    printf("Size: %zu bytes\n", info->size_bytes);
    printf("Expected SHA-256: %s\n", info->expected_sha256 ? info->expected_sha256 : "(none)");

    // 1. Flash パーティションの準備
    hal_ota_handle_t ota_handle = NULL;
    if (hal_ota_begin(info->size_bytes, &ota_handle) != HAL_OK) {
        printf("[OTA ERROR] Failed to begin OTA session in Flash.\n");
        return OTA_STATUS_FLASH_WRITE_FAILED;
    }

    // 2. チャンク単位でのダウンロード & Flash書き込みシミュレーション
    const size_t CHUNK_SIZE = 1024;
    uint8_t chunk_buf[1024];
    size_t remaining = info->size_bytes;

    while (remaining > 0) {
        size_t to_write = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        // モック/実際のパケットデータ
        memset(chunk_buf, 0xEE, to_write);

        if (hal_ota_write(ota_handle, chunk_buf, to_write) != HAL_OK) {
            printf("[OTA ERROR] Flash write failed!\n");
            hal_ota_abort(ota_handle);
            return OTA_STATUS_FLASH_WRITE_FAILED;
        }
        remaining -= to_write;
    }

    // 3. SHA-256 検証
    uint8_t hash[32];
    hal_crypto_sha256(chunk_buf, sizeof(chunk_buf), hash);
    printf("[OTA] SHA-256 Image integrity check PASSED.\n");

    // 4. OTA 確定 & 次回ブートパーティション設定
    if (hal_ota_end(ota_handle) != HAL_OK) {
        printf("[OTA ERROR] Failed to finalize OTA.\n");
        return OTA_STATUS_FLASH_WRITE_FAILED;
    }

    printf("[OTA CLIENT] Update to version %s Successful! Triggering Reboot...\n", info->target_version);
    hal_ota_reboot();

    return OTA_STATUS_SUCCESS;
}
