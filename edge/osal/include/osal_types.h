/**
 * @file osal_types.h
 * @brief OS Abstraction Layer (OSAL) 共通型定義・エラーコード
 */

#ifndef OSAL_TYPES_H
#define OSAL_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief OSAL戻り値ステータス */
typedef enum {
    OSAL_OK                 = 0,   /**< 成功 */
    OSAL_ERROR              = -1,  /**< 一般エラー */
    OSAL_ERR_TIMEOUT        = -2,  /**< タイムアウト発生 */
    OSAL_ERR_INVALID_PARAM  = -3,  /**< 不正な引数 */
    OSAL_ERR_NO_MEMORY      = -4,  /**< メモリ不足 */
    OSAL_ERR_RESOURCE_BUSY  = -5,  /**< リソース使用中 */
    OSAL_ERR_NOT_SUPPORTED  = -6   /**< 未サポート機能 */
} osal_status_t;

/** @brief 無限待機定数 */
#define OSAL_WAIT_FOREVER   0xFFFFFFFFU

/** @brief 待機なし定数 */
#define OSAL_NO_WAIT        0x00000000U

#ifdef __cplusplus
}
#endif

#endif // OSAL_TYPES_H
