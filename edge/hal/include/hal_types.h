/**
 * @file hal_types.h
 * @brief Hardware Abstraction Layer (HAL) 共通型定義・エラーコード
 */

#ifndef HAL_TYPES_H
#define HAL_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_OK                 = 0,
    HAL_ERROR              = -1,
    HAL_ERR_TIMEOUT        = -2,
    HAL_ERR_INVALID_PARAM  = -3,
    HAL_ERR_BUSY           = -4,
    HAL_ERR_NOT_READY      = -5,
    HAL_ERR_NOT_SUPPORTED  = -6
} hal_status_t;

#ifdef __cplusplus
}
#endif

#endif // HAL_TYPES_H
