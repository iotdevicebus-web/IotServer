/**
 * @file hal_i2c.h
 * @brief HAL I2C バスインターフェース (温湿度・センサ用)
 */

#ifndef HAL_I2C_H
#define HAL_I2C_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t hal_i2c_port_t;

typedef struct {
    uint32_t sda_pin;
    uint32_t scl_pin;
    uint32_t clock_speed_hz; /**< 100000 (Standard) or 400000 (Fast) */
} hal_i2c_config_t;

hal_status_t hal_i2c_init(hal_i2c_port_t port, const hal_i2c_config_t *config);
hal_status_t hal_i2c_deinit(hal_i2c_port_t port);

hal_status_t hal_i2c_write(
    hal_i2c_port_t port,
    uint8_t dev_addr,
    const uint8_t *data,
    size_t len,
    uint32_t timeout_ms
);

hal_status_t hal_i2c_read(
    hal_i2c_port_t port,
    uint8_t dev_addr,
    uint8_t *data,
    size_t len,
    uint32_t timeout_ms
);

hal_status_t hal_i2c_write_read(
    hal_i2c_port_t port,
    uint8_t dev_addr,
    const uint8_t *write_data,
    size_t write_len,
    uint8_t *read_data,
    size_t read_len,
    uint32_t timeout_ms
);

#ifdef __cplusplus
}
#endif

#endif // HAL_I2C_H
