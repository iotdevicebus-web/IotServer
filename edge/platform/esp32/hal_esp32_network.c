/**
 * @file hal_esp32_network.c
 * @brief ESP32 (ESP-IDF) 向け HAL ネットワーク & mTLS HTTPS POST 実装
 */

#include "hal_network.h"
#include <string.h>



#if defined(ESP_PLATFORM)
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"

static const char *TAG = "HAL_ESP32_NET";
static hal_net_link_status_t s_link_status = HAL_NET_LINK_DOWN;

hal_status_t hal_network_init(void) {
    ESP_LOGI(TAG, "Initializing Wi-Fi subsystem...");
    s_link_status = HAL_NET_LINK_DOWN;
    return HAL_OK;
}

hal_status_t hal_network_connect(void) {
    ESP_LOGI(TAG, "Connecting to Wi-Fi AP...");
    esp_wifi_connect();
    s_link_status = HAL_NET_LINK_UP;
    return HAL_OK;
}

hal_status_t hal_network_disconnect(void) {
    esp_wifi_disconnect();
    s_link_status = HAL_NET_LINK_DOWN;
    return HAL_OK;
}

hal_net_link_status_t hal_network_get_status(void) {
    return s_link_status;
}

hal_status_t hal_network_get_rssi(int *out_rssi_dbm) {
    if (!out_rssi_dbm) return HAL_ERR_INVALID_PARAM;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        *out_rssi_dbm = ap_info.rssi;
        return HAL_OK;
    }
    *out_rssi_dbm = -70; // フォールバック
    return HAL_OK;
}

hal_status_t hal_https_post(
    const hal_http_post_request_t *request,
    hal_http_response_t *response
) {
    if (!request || !response || !request->url || !request->payload) {
        return HAL_ERR_INVALID_PARAM;
    }

    esp_http_client_config_t config = {
        .url = request->url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = request->timeout_ms ? request->timeout_ms : 5000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };

    // mTLS 証明書設定
    if (request->tls_creds) {
        config.cert_pem = request->tls_creds->root_ca_pem;
        config.client_cert_pem = request->tls_creds->client_cert_pem;
        config.client_key_pem = request->tls_creds->client_key_pem;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return HAL_ERROR;
    }

    esp_http_client_set_header(client, "Content-Type", request->content_type ? request->content_type : "application/json");
    esp_http_client_set_post_field(client, (const char *)request->payload, (int)request->payload_len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        response->status_code = esp_http_client_get_status_code(client);
        int read_len = esp_http_client_read(client, (char *)response->response_buffer, (int)response->buffer_size - 1);
        if (read_len >= 0) {
            response->received_len = (size_t)read_len;
            response->response_buffer[read_len] = '\0';
        }
        ESP_LOGI(TAG, "HTTPS POST executed. Status: %d, Response: %d bytes", response->status_code, read_len);
    } else {
        ESP_LOGE(TAG, "HTTPS POST failed: %s", esp_err_to_name(err));
        response->status_code = -1;
    }

    esp_http_client_cleanup(client);
    return (err == ESP_OK) ? HAL_OK : HAL_ERROR;
}

#endif // ESP_PLATFORM
