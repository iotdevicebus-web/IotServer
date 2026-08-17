/**
 * @file hal_ota.h
 * @brief HAL OTA (Over-The-Air) フラッシュ書き込み・ブート制御インターフェース
 */

#ifndef HAL_OTA_H
#define HAL_OTA_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* hal_ota_handle_t;

/**
 * @brief OTA更新セッションを開始し、書き込み先パーティションを準備する
 * @param image_size バイナリの総バイト数
 * @param out_handle OTAセッションハンドル
 */
hal_status_t hal_ota_begin(size_t image_size, hal_ota_handle_t *out_handle);

/**
 * @brief 受信したファームウェアチャンクをFlashに書き込む
 */
hal_status_t hal_ota_write(hal_ota_handle_t handle, const uint8_t *data, size_t len);

/**
 * @brief OTA書き込みを完了し、次回ブートパーティションに設定する
 */
hal_status_t hal_ota_end(hal_ota_handle_t handle);

/**
 * @brief OTAセッションを中断・破棄する
 */
hal_status_t hal_ota_abort(hal_ota_handle_t handle);

/**
 * @brief 新ファームウェアでシステムを再起動する
 */
void hal_ota_reboot(void);

#ifdef __cplusplus
}
#endif

#endif // HAL_OTA_H
