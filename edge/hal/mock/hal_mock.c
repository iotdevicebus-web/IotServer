/**
 * @file hal_mock.c
 * @brief Hardware Abstraction Layer (HAL) PC用モック実装
 */

#include "hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- 内部モックステート --- */
static struct {
    bool gpio_levels[64];
    hal_gpio_isr_t gpio_isrs[64];
    void *gpio_args[64];
    hal_net_link_status_t net_status;
    hal_wakeup_cause_t wakeup_cause;
    uint32_t mock_voltage_mv;
    float mock_temp;
    float mock_humi;
} s_mock_state = {
    .net_status = HAL_NET_LINK_UP,
    .wakeup_cause = HAL_WAKEUP_CAUSE_POWER_ON,
    .mock_voltage_mv = 3950, // 3.95V (LiPoバッテリ)
    .mock_temp = 24.5f,
    .mock_humi = 55.0f
};

/* --- HAL Base --- */
hal_status_t hal_init(void) {
    printf("[HAL MOCK] Hardware Initialized.\n");
    return HAL_OK;
}

/* --- GPIO Mock --- */
hal_status_t hal_gpio_init(hal_gpio_pin_t pin, hal_gpio_mode_t mode, hal_gpio_pull_t pull) {
    (void)mode; (void)pull;
    if (pin >= 64) return HAL_ERR_INVALID_PARAM;
    s_mock_state.gpio_levels[pin] = false;
    return HAL_OK;
}

hal_status_t hal_gpio_write(hal_gpio_pin_t pin, bool level) {
    if (pin >= 64) return HAL_ERR_INVALID_PARAM;
    s_mock_state.gpio_levels[pin] = level;
    return HAL_OK;
}

hal_status_t hal_gpio_read(hal_gpio_pin_t pin, bool *out_level) {
    if (pin >= 64 || !out_level) return HAL_ERR_INVALID_PARAM;
    *out_level = s_mock_state.gpio_levels[pin];
    return HAL_OK;
}

hal_status_t hal_gpio_toggle(hal_gpio_pin_t pin) {
    if (pin >= 64) return HAL_ERR_INVALID_PARAM;
    s_mock_state.gpio_levels[pin] = !s_mock_state.gpio_levels[pin];
    return HAL_OK;
}

hal_status_t hal_gpio_attach_interrupt(
    hal_gpio_pin_t pin,
    hal_gpio_intr_type_t intr_type,
    hal_gpio_isr_t isr_func,
    void *arg
) {
    (void)intr_type;
    if (pin >= 64) return HAL_ERR_INVALID_PARAM;
    s_mock_state.gpio_isrs[pin] = isr_func;
    s_mock_state.gpio_args[pin] = arg;
    return HAL_OK;
}

hal_status_t hal_gpio_detach_interrupt(hal_gpio_pin_t pin) {
    if (pin >= 64) return HAL_ERR_INVALID_PARAM;
    s_mock_state.gpio_isrs[pin] = NULL;
    return HAL_OK;
}

/** @brief テスト用: 外部割り込みの擬似発火 */
void hal_mock_trigger_gpio_interrupt(hal_gpio_pin_t pin) {
    if (pin < 64 && s_mock_state.gpio_isrs[pin]) {
        printf("[HAL MOCK] Triggering GPIO ISR for Pin %u\n", pin);
        s_mock_state.gpio_isrs[pin](pin, s_mock_state.gpio_args[pin]);
    }
}

/* --- I2C Mock (温湿度センサ等) --- */
hal_status_t hal_i2c_init(hal_i2c_port_t port, const hal_i2c_config_t *config) {
    (void)port; (void)config;
    return HAL_OK;
}

hal_status_t hal_i2c_deinit(hal_i2c_port_t port) {
    (void)port;
    return HAL_OK;
}

hal_status_t hal_i2c_write(hal_i2c_port_t port, uint8_t dev_addr, const uint8_t *data, size_t len, uint32_t timeout_ms) {
    (void)port; (void)dev_addr; (void)data; (void)len; (void)timeout_ms;
    return HAL_OK;
}

hal_status_t hal_i2c_read(hal_i2c_port_t port, uint8_t dev_addr, uint8_t *data, size_t len, uint32_t timeout_ms) {
    (void)port; (void)dev_addr; (void)timeout_ms;
    if (!data) return HAL_ERR_INVALID_PARAM;
    // 擬似センサデータ返却
    memset(data, 0, len);
    return HAL_OK;
}

hal_status_t hal_i2c_write_read(hal_i2c_port_t port, uint8_t dev_addr, const uint8_t *write_data, size_t write_len, uint8_t *read_data, size_t read_len, uint32_t timeout_ms) {
    (void)port; (void)dev_addr; (void)write_data; (void)write_len; (void)read_data; (void)read_len; (void)timeout_ms;
    return HAL_OK;
}

/* --- ADC Mock (バッテリ電圧) --- */
hal_status_t hal_adc_init(hal_adc_channel_t channel) {
    (void)channel;
    return HAL_OK;
}

hal_status_t hal_adc_read_raw(hal_adc_channel_t channel, uint32_t *out_raw) {
    (void)channel;
    if (!out_raw) return HAL_ERR_INVALID_PARAM;
    *out_raw = (s_mock_state.mock_voltage_mv * 4095) / 5000;
    return HAL_OK;
}

hal_status_t hal_adc_read_voltage_mv(hal_adc_channel_t channel, uint32_t *out_mv) {
    (void)channel;
    if (!out_mv) return HAL_ERR_INVALID_PARAM;
    *out_mv = s_mock_state.mock_voltage_mv;
    return HAL_OK;
}

/* --- Network & mTLS HTTPS Mock --- */
hal_status_t hal_network_init(void) {
    return HAL_OK;
}

hal_status_t hal_network_connect(void) {
    s_mock_state.net_status = HAL_NET_LINK_UP;
    printf("[HAL MOCK] Network connected (Link UP).\n");
    return HAL_OK;
}

hal_status_t hal_network_disconnect(void) {
    s_mock_state.net_status = HAL_NET_LINK_DOWN;
    return HAL_OK;
}

hal_net_link_status_t hal_network_get_status(void) {
    return s_mock_state.net_status;
}

hal_status_t hal_network_get_rssi(int *out_rssi_dbm) {
    if (!out_rssi_dbm) return HAL_ERR_INVALID_PARAM;
    *out_rssi_dbm = -62; // 良好な電波強度
    return HAL_OK;
}

hal_status_t hal_https_post(
    const hal_http_post_request_t *request,
    hal_http_response_t *response
) {
    if (!request || !response || !request->payload) {
        return HAL_ERR_INVALID_PARAM;
    }

    printf("\n[HAL MOCK] === Outgoing mTLS HTTPS POST ===\n");
    printf("URL: %s\n", request->url ? request->url : "(null)");
    printf("Content-Type: %s\n", request->content_type ? request->content_type : "(null)");
    printf("Payload (%zu bytes): %.*s\n", request->payload_len, (int)request->payload_len, (char *)request->payload);
    printf("===========================================\n");

    // モックサーバ応答を生成 (JSON ApiResponse)
    const char *mock_server_json = 
        "{\"status\":\"OK\",\"message\":\"Telemetry accepted\",\"server_time\":1755420000,\"sleep_interval_sec\":60}";
    
    size_t resp_len = strlen(mock_server_json);
    response->status_code = 200;
    
    if (response->response_buffer && response->buffer_size > resp_len) {
        memcpy(response->response_buffer, mock_server_json, resp_len + 1);
        response->received_len = resp_len;
    }

    return HAL_OK;
}

/* --- Sleep Mock --- */
hal_status_t hal_sleep_init(void) {
    return HAL_OK;
}

hal_status_t hal_sleep_enable_timer_wakeup(uint32_t sleep_duration_sec) {
    printf("[HAL MOCK] Sleep Timer configured for %u seconds.\n", sleep_duration_sec);
    return HAL_OK;
}

hal_status_t hal_sleep_enable_gpio_wakeup(hal_gpio_pin_t pin, hal_gpio_intr_type_t trigger_type) {
    (void)trigger_type;
    printf("[HAL MOCK] GPIO Wakeup configured for Pin %u.\n", pin);
    return HAL_OK;
}

void hal_sleep_enter(hal_sleep_mode_t mode) {
    const char *mode_str = (mode == HAL_SLEEP_MODE_DEEP) ? "DEEP SLEEP" : "LIGHT SLEEP";
    printf("[HAL MOCK] >>> Entering %s <<<\n", mode_str);
    s_mock_state.wakeup_cause = HAL_WAKEUP_CAUSE_TIMER;
}

hal_wakeup_cause_t hal_sleep_get_wakeup_cause(void) {
    return s_mock_state.wakeup_cause;
}

/* --- Crypto Mock --- */
hal_status_t hal_crypto_init(void) {
    return HAL_OK;
}

hal_status_t hal_crypto_get_random(uint8_t *buffer, size_t len) {
    if (!buffer) return HAL_ERR_INVALID_PARAM;
    for (size_t i = 0; i < len; i++) {
        buffer[i] = (uint8_t)(rand() % 256);
    }
    return HAL_OK;
}

hal_status_t hal_crypto_sha256(const uint8_t *input, size_t input_len, uint8_t *output_hash_32b) {
    if (!input || !output_hash_32b) return HAL_ERR_INVALID_PARAM;
    // 簡易ハッシュモック (先頭と末尾の簡易演算)
    memset(output_hash_32b, 0xAA, 32);
    for (size_t i = 0; i < input_len; i++) {
        output_hash_32b[i % 32] ^= input[i];
    }
    return HAL_OK;
}

static bool s_se_ready = false;

hal_status_t hal_crypto_se_init(void) {
    printf("[HAL MOCK SE] Secure Element (ATECC608A / OPTIGA Mock) Initialized.\n");
    s_se_ready = true;
    return HAL_OK;
}

hal_status_t hal_crypto_se_sign_digest(uint8_t slot_id, const uint8_t *digest_32b, uint8_t *out_sig_64b) {
    if (!s_se_ready || !digest_32b || !out_sig_64b) return HAL_ERR_NOT_READY;
    printf("[HAL MOCK SE] Generating ECDSA-P256 signature internally on Slot %d (Private key never leaves chip!)...\n", slot_id);
    // モック署名 (R: digest XOR 0x55, S: digest XOR 0x33)
    for (int i = 0; i < 32; i++) {
        out_sig_64b[i] = digest_32b[i] ^ 0x55;
        out_sig_64b[32 + i] = digest_32b[i] ^ 0x33;
    }
    return HAL_OK;
}

hal_status_t hal_crypto_se_get_public_key(uint8_t slot_id, uint8_t *out_pubkey_64b) {
    if (!s_se_ready || !out_pubkey_64b) return HAL_ERR_NOT_READY;
    printf("[HAL MOCK SE] Reading Public Key from Slot %d (64 bytes)...\n", slot_id);
    memset(out_pubkey_64b, 0xBB, 64);
    return HAL_OK;
}


bool hal_crypto_se_is_ready(void) {
    return s_se_ready;
}

/* --- OTA Mock --- */
typedef struct {
    size_t total_size;
    size_t written_size;
} mock_ota_session_t;

hal_status_t hal_ota_begin(size_t image_size, hal_ota_handle_t *out_handle) {
    if (!out_handle || image_size == 0) return HAL_ERR_INVALID_PARAM;
    mock_ota_session_t *s = (mock_ota_session_t *)malloc(sizeof(mock_ota_session_t));
    if (!s) return HAL_ERR_INVALID_PARAM;
    s->total_size = image_size;
    s->written_size = 0;
    printf("[HAL MOCK OTA] Session started. Expecting %zu bytes.\n", image_size);
    *out_handle = (hal_ota_handle_t)s;
    return HAL_OK;
}

hal_status_t hal_ota_write(hal_ota_handle_t handle, const uint8_t *data, size_t len) {
    if (!handle || !data) return HAL_ERR_INVALID_PARAM;
    mock_ota_session_t *s = (mock_ota_session_t *)handle;
    s->written_size += len;
    printf("[HAL MOCK OTA] Wrote chunk: %zu bytes (Progress: %zu / %zu bytes)\n", len, s->written_size, s->total_size);
    return HAL_OK;
}

hal_status_t hal_ota_end(hal_ota_handle_t handle) {
    if (!handle) return HAL_ERR_INVALID_PARAM;
    mock_ota_session_t *s = (mock_ota_session_t *)handle;
    printf("[HAL MOCK OTA] Finalized OTA. Written %zu bytes successfully.\n", s->written_size);
    free(s);
    return HAL_OK;
}

hal_status_t hal_ota_abort(hal_ota_handle_t handle) {
    if (handle) free(handle);
    printf("[HAL MOCK OTA] Aborted.\n");
    return HAL_OK;
}

void hal_ota_reboot(void) {
    printf("[HAL MOCK OTA] >>> REBOOTING INTO NEW FIRMWARE <<<\n");
}

