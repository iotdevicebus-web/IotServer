/**
 * @file hal_esp32_sleep.c
 * @brief ESP32 向け HAL スリープ・省電力制御実装
 */

#include "hal_sleep.h"

#if defined(ESP_PLATFORM)
#include "esp_sleep.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"


static const char *TAG = "HAL_ESP32_SLEEP";

hal_status_t hal_sleep_init(void) {
    return HAL_OK;
}

hal_status_t hal_sleep_enable_timer_wakeup(uint32_t sleep_duration_sec) {
    esp_err_t err = esp_sleep_enable_timer_wakeup((uint64_t)sleep_duration_sec * 1000000ULL);
    ESP_LOGI(TAG, "Configured RTC Timer wakeup for %u sec", sleep_duration_sec);
    return (err == ESP_OK) ? HAL_OK : HAL_ERROR;
}

hal_status_t hal_sleep_enable_gpio_wakeup(hal_gpio_pin_t pin, hal_gpio_intr_type_t trigger_type) {
    gpio_pullup_en((gpio_num_t)pin);
    gpio_pulldown_dis((gpio_num_t)pin);
    esp_sleep_ext1_wakeup_mode_t mode = (trigger_type == HAL_GPIO_INTR_LOW_LEVEL) ? 
                                        ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH;
    esp_err_t err = esp_sleep_enable_ext1_wakeup(1ULL << pin, mode);
    ESP_LOGI(TAG, "Configured EXT1 GPIO wakeup on Pin %u", pin);
    return (err == ESP_OK) ? HAL_OK : HAL_ERROR;
}

hal_status_t hal_sleep_disable_gpio_interrupt(hal_gpio_pin_t pin) {
    gpio_intr_disable((gpio_num_t)pin);
    rtc_gpio_deinit((gpio_num_t)pin);
    ESP_LOGI(TAG, "Disabled GPIO %u interrupt during active processing", pin);
    return HAL_OK;
}


void hal_sleep_enter(hal_sleep_mode_t mode) {
    if (mode == HAL_SLEEP_MODE_DEEP) {
        ESP_LOGI(TAG, ">>> Entering ESP32 Deep Sleep <<<");
        esp_deep_sleep_start();
    } else {
        ESP_LOGI(TAG, ">>> Entering ESP32 Light Sleep <<<");
        esp_light_sleep_start();
    }
}

hal_wakeup_cause_t hal_sleep_get_wakeup_cause(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            return HAL_WAKEUP_CAUSE_TIMER;
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
        case ESP_SLEEP_WAKEUP_GPIO:
            return HAL_WAKEUP_CAUSE_GPIO;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            return HAL_WAKEUP_CAUSE_POWER_ON;
        default:
            return HAL_WAKEUP_CAUSE_UNKNOWN;
    }
}

#endif // ESP_PLATFORM
