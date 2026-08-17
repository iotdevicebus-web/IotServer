/**
 * @file hal_network.h
 * @brief HAL ネットワーク & mTLS HTTPS POST クライアントインターフェース
 */

#ifndef HAL_NETWORK_H
#define HAL_NETWORK_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_NET_LINK_DOWN,
    HAL_NET_LINK_CONNECTING,
    HAL_NET_LINK_UP,
    HAL_NET_LINK_ERROR
} hal_net_link_status_t;

typedef struct {
    const char *ssid;
    const char *password;
} hal_wifi_config_t;

typedef struct {
    const char *apn;
} hal_cellular_config_t;

/** @brief mTLS 証明書・TLS設定 */
typedef struct {
    const char *root_ca_pem;       /**< サーバ検証用 Root CA (PEM) */
    const char *client_cert_pem;   /**< デバイス固有クライアント証明書 (PEM) */
    const char *client_key_pem;    /**< デバイス固有秘密鍵 (PEM) */
} hal_tls_credentials_t;

/** @brief HTTPS POST 要求設定 */
typedef struct {
    const char *url;               /**< 例: "https://iot-server.local/api/v1/telemetry" */
    const char *content_type;      /**< 例: "application/json" */
    const uint8_t *payload;        /**< 送信データバッファ */
    size_t payload_len;            /**< 送信データ長 */
    const hal_tls_credentials_t *tls_creds; /**< mTLS 認証情報 */
    uint32_t timeout_ms;           /**< タイムアウト */
} hal_http_post_request_t;

/** @brief HTTPS 応答データ */
typedef struct {
    int status_code;               /**< HTTP ステータスコード (200, 400, etc.) */
    uint8_t *response_buffer;      /**< 応答格納先バッファ */
    size_t buffer_size;            /**< バッファ最大容量 */
    size_t received_len;           /**< 実際に受信したバイト数 */
} hal_http_response_t;

/* --- ネットワーク接続管理 --- */
hal_status_t hal_network_init(void);
hal_status_t hal_network_connect(void);
hal_status_t hal_network_disconnect(void);
hal_net_link_status_t hal_network_get_status(void);
hal_status_t hal_network_get_rssi(int *out_rssi_dbm);

/* --- mTLS HTTPS POST 送信 --- */
hal_status_t hal_https_post(
    const hal_http_post_request_t *request,
    hal_http_response_t *response
);

#ifdef __cplusplus
}
#endif

#endif // HAL_NETWORK_H
