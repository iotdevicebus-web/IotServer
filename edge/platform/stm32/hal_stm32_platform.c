/**
 * @file hal_stm32_platform.c
 * @brief STM32 (HAL / LL) 向け ハードウェア抽象化層実装
 */

#include "hal.h"
#include <string.h>

#if defined(STM32) || defined(__arm__)

hal_status_t hal_init(void) {
    // HAL_Init(); SystemClock_Config();
    return HAL_OK;
}

hal_status_t hal_gpio_init(hal_gpio_pin_t pin, hal_gpio_mode_t mode, hal_gpio_pull_t pull) {
    (void)pin; (void)mode; (void)pull;
    // GPIO_InitStruct設定
    return HAL_OK;
}

hal_status_t hal_gpio_write(hal_gpio_pin_t pin, bool level) {
    (void)pin; (void)level;
    // HAL_GPIO_WritePin(...)
    return HAL_OK;
}

hal_status_t hal_gpio_read(hal_gpio_pin_t pin, bool *out_level) {
    (void)pin;
    if (!out_level) return HAL_ERR_INVALID_PARAM;
    *out_level = true;
    return HAL_OK;
}

hal_status_t hal_gpio_toggle(hal_gpio_pin_t pin) {
    (void)pin;
    return HAL_OK;
}

hal_status_t hal_gpio_attach_interrupt(hal_gpio_pin_t pin, hal_gpio_intr_type_t intr_type, hal_gpio_isr_t isr_func, void *arg) {
    (void)pin; (void)intr_type; (void)isr_func; (void)arg;
    return HAL_OK;
}

hal_status_t hal_gpio_detach_interrupt(hal_gpio_pin_t pin) {
    (void)pin;
    return HAL_OK;
}

hal_status_t hal_sleep_init(void) {
    return HAL_OK;
}

hal_status_t hal_sleep_enable_timer_wakeup(uint32_t sleep_duration_sec) {
    (void)sleep_duration_sec;
    // HAL_RTCEx_SetWakeUpTimer_IT(...)
    return HAL_OK;
}

hal_status_t hal_sleep_enable_gpio_wakeup(hal_gpio_pin_t pin, hal_gpio_intr_type_t trigger_type) {
    (void)pin; (void)trigger_type;
    // HAL_PWR_EnableWakeUpPin(...)
    return HAL_OK;
}

void hal_sleep_enter(hal_sleep_mode_t mode) {
    (void)mode;
    // HAL_PWR_EnterSTOPMode / HAL_PWR_EnterSTANDBYMode
}

hal_wakeup_cause_t hal_sleep_get_wakeup_cause(void) {
    return HAL_WAKEUP_CAUSE_TIMER;
}

hal_status_t hal_adc_init(hal_adc_channel_t channel) {
    (void)channel;
    return HAL_OK;
}

hal_status_t hal_adc_read_voltage_mv(hal_adc_channel_t channel, uint32_t *out_mv) {
    (void)channel;
    if (!out_mv) return HAL_ERR_INVALID_PARAM;
    *out_mv = 3300; // 3.3V
    return HAL_OK;
}

hal_status_t hal_network_init(void) { return HAL_OK; }
hal_status_t hal_network_connect(void) { return HAL_OK; }
hal_status_t hal_network_disconnect(void) { return HAL_OK; }
hal_net_link_status_t hal_network_get_status(void) { return HAL_NET_LINK_UP; }
hal_status_t hal_network_get_rssi(int *out_rssi) {
    if (out_rssi) *out_rssi = -60;
    return HAL_OK;
}

hal_status_t hal_https_post(const hal_http_post_request_t *request, hal_http_response_t *response) {
    (void)request;
    if (response) {
        response->status_code = 200;
    }
    return HAL_OK;
}

hal_status_t hal_ota_begin(size_t image_size, hal_ota_handle_t *out_handle) {
    (void)image_size;
    if (out_handle) *out_handle = (hal_ota_handle_t)0x1;
    return HAL_OK;
}

hal_status_t hal_ota_write(hal_ota_handle_t handle, const uint8_t *data, size_t len) {
    (void)handle; (void)data; (void)len;
    return HAL_OK;
}

hal_status_t hal_ota_end(hal_ota_handle_t handle) {
    (void)handle;
    return HAL_OK;
}

hal_status_t hal_ota_abort(hal_ota_handle_t handle) {
    (void)handle;
    return HAL_OK;
}

void hal_ota_reboot(void) {
    // NVIC_SystemReset();
}


#endif // STM32 / __arm__
