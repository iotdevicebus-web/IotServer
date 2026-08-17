/**
 * @file hal_adc.h
 * @brief HAL ADC (アナログ・デジタル変換) インターフェース (バッテリ電圧監視等)
 */

#ifndef HAL_ADC_H
#define HAL_ADC_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t hal_adc_channel_t;

hal_status_t hal_adc_init(hal_adc_channel_t channel);
hal_status_t hal_adc_read_raw(hal_adc_channel_t channel, uint32_t *out_raw);
hal_status_t hal_adc_read_voltage_mv(hal_adc_channel_t channel, uint32_t *out_mv);

#ifdef __cplusplus
}
#endif

#endif // HAL_ADC_H
