/**
 * @file hal_gpio.h
 * @brief HAL GPIO・外部割り込みインターフェース
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t hal_gpio_pin_t;

typedef enum {
    HAL_GPIO_MODE_INPUT,
    HAL_GPIO_MODE_OUTPUT,
    HAL_GPIO_MODE_OUTPUT_OD
} hal_gpio_mode_t;

typedef enum {
    HAL_GPIO_PULL_NONE,
    HAL_GPIO_PULL_UP,
    HAL_GPIO_PULL_DOWN
} hal_gpio_pull_t;

typedef enum {
    HAL_GPIO_INTR_DISABLE,
    HAL_GPIO_INTR_RISING,
    HAL_GPIO_INTR_FALLING,
    HAL_GPIO_INTR_BOTH,
    HAL_GPIO_INTR_LOW_LEVEL,
    HAL_GPIO_INTR_HIGH_LEVEL
} hal_gpio_intr_type_t;

typedef void (*hal_gpio_isr_t)(hal_gpio_pin_t pin, void *arg);

hal_status_t hal_gpio_init(hal_gpio_pin_t pin, hal_gpio_mode_t mode, hal_gpio_pull_t pull);
hal_status_t hal_gpio_write(hal_gpio_pin_t pin, bool level);
hal_status_t hal_gpio_read(hal_gpio_pin_t pin, bool *out_level);
hal_status_t hal_gpio_toggle(hal_gpio_pin_t pin);

/**
 * @brief GPIO割り込みコールバックの登録
 */
hal_status_t hal_gpio_attach_interrupt(
    hal_gpio_pin_t pin,
    hal_gpio_intr_type_t intr_type,
    hal_gpio_isr_t isr_func,
    void *arg
);

hal_status_t hal_gpio_detach_interrupt(hal_gpio_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif // HAL_GPIO_H
