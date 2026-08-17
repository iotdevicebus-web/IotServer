/**
 * @file hal_sleep.h
 * @brief HAL 省電力・スリープ制御・Wakeup要因設定インターフェース
 */

#ifndef HAL_SLEEP_H
#define HAL_SLEEP_H

#include "hal_types.h"
#include "hal_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_SLEEP_MODE_LIGHT,   /**< RAM保持・高速復帰 (Light Sleep) */
    HAL_SLEEP_MODE_DEEP,    /**< 超低消費電力・リセット復帰 (Deep Sleep) */
    HAL_SLEEP_MODE_STANDBY  /**< 最小電力・RTC保持 */
} hal_sleep_mode_t;

typedef enum {
    HAL_WAKEUP_CAUSE_UNKNOWN,
    HAL_WAKEUP_CAUSE_TIMER,
    HAL_WAKEUP_CAUSE_GPIO,
    HAL_WAKEUP_CAUSE_POWER_ON,
    HAL_WAKEUP_CAUSE_WATCHDOG
} hal_wakeup_cause_t;

/**
 * @brief スリープ初期化
 */
hal_status_t hal_sleep_init(void);

/**
 * @brief タイマーによる起床 (Wakeup) を有効化
 * @param sleep_duration_sec 起床までの秒数
 */
hal_status_t hal_sleep_enable_timer_wakeup(uint32_t sleep_duration_sec);

/**
 * @brief GPIO割り込みによる起床 (Wakeup) を有効化
 */
hal_status_t hal_sleep_enable_gpio_wakeup(hal_gpio_pin_t pin, hal_gpio_intr_type_t trigger_type);

/**
 * @brief 指定したスリープモードへ移行する
 * @note Deep Sleep の場合、この関数から復帰せず起床時に再起動します。
 */
void hal_sleep_enter(hal_sleep_mode_t mode);

/**
 * @brief 今回の起床要因を取得する
 */
hal_wakeup_cause_t hal_sleep_get_wakeup_cause(void);

#ifdef __cplusplus
}
#endif

#endif // HAL_SLEEP_H
