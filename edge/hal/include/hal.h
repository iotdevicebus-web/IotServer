/**
 * @file hal.h
 * @brief HAL (Hardware Abstraction Layer) 総合ヘッダ
 */

#ifndef HAL_H
#define HAL_H

#include "hal_types.h"
#include "hal_gpio.h"
#include "hal_i2c.h"
#include "hal_adc.h"
#include "hal_network.h"
#include "hal_sleep.h"
#include "hal_crypto.h"
#include "hal_ota.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ハードウェア共通初期化
 */
hal_status_t hal_init(void);

#ifdef __cplusplus
}
#endif

#endif // HAL_H
