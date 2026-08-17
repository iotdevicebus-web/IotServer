#!/usr/bin/env bash
set -euo pipefail

# デフォルト設定
DEVICE_ID="${1:-DEV-ESP32-001}"
DAYS="${2:-365}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/out"

mkdir -p "${OUTPUT_DIR}"

echo "========================================="
echo "  IoT Platform mTLS 証明書生成ツール (Bash)"
echo "========================================="

# 1. Root CA
CA_KEY="${OUTPUT_DIR}/ca.key"
CA_CRT="${OUTPUT_DIR}/ca.crt"

if [ ! -f "${CA_CRT}" ]; then
    echo "[1/3] Root CA (プライベート認証局) を生成中..."
    openssl req -x509 -new -nodes -config "${SCRIPT_DIR}/ca.cnf" -keyout "${CA_KEY}" -out "${CA_CRT}" -days "$((DAYS * 5))"
    echo "  -> Root CA 作成完了: ${CA_CRT}"
else
    echo "[1/3] 既存の Root CA を使用します: ${CA_CRT}"
fi

# 2. Server Certificate
SERVER_KEY="${OUTPUT_DIR}/server.key"
SERVER_CSR="${OUTPUT_DIR}/server.csr"
SERVER_CRT="${OUTPUT_DIR}/server.crt"

echo "[2/3] サーバ証明書を発行中 (localhost, 127.0.0.1)..."
openssl req -new -nodes -config "${SCRIPT_DIR}/server.cnf" -keyout "${SERVER_KEY}" -out "${SERVER_CSR}"
openssl x509 -req -in "${SERVER_CSR}" -CA "${CA_CRT}" -CAkey "${CA_KEY}" -CAcreateserial -out "${SERVER_CRT}" -days "${DAYS}" -extfile "${SCRIPT_DIR}/server.cnf" -extensions v3_req
rm -f "${SERVER_CSR}"
echo "  -> サーバ証明書発行完了: ${SERVER_CRT}"

# 3. Client Certificate for Device
CLIENT_KEY="${OUTPUT_DIR}/client_${DEVICE_ID}.key"
CLIENT_CSR="${OUTPUT_DIR}/client_${DEVICE_ID}.csr"
CLIENT_CRT="${OUTPUT_DIR}/client_${DEVICE_ID}.crt"
DEVICE_CNF="${OUTPUT_DIR}/temp_${DEVICE_ID}.cnf"

echo "[3/3] クライアント証明書を発行中 (DeviceId: ${DEVICE_ID})..."
sed "s/DEV-ESP32-001/${DEVICE_ID}/g" "${SCRIPT_DIR}/client.cnf" > "${DEVICE_CNF}"
openssl req -new -nodes -config "${DEVICE_CNF}" -keyout "${CLIENT_KEY}" -out "${CLIENT_CSR}"
openssl x509 -req -in "${CLIENT_CSR}" -CA "${CA_CRT}" -CAkey "${CA_KEY}" -CAcreateserial -out "${CLIENT_CRT}" -days "${DAYS}" -extfile "${DEVICE_CNF}" -extensions v3_req
rm -f "${CLIENT_CSR}" "${DEVICE_CNF}"
echo "  -> クライアント証明書発行完了: ${CLIENT_CRT}"

# 4. エッジファームウェア向け C言語ヘッダファイルの生成
HEADER_FILE="${OUTPUT_DIR}/device_certs.h"
echo "エッジ用 C言語ヘッダ (${HEADER_FILE}) を出力中..."

cat << EOF > "${HEADER_FILE}"
/**
 * @file device_certs.h
 * @brief Auto-generated mTLS certificates for ${DEVICE_ID}
 * @note Generated at $(date '+%Y-%m-%d %H:%M:%S')
 */
#ifndef DEVICE_CERTS_H
#define DEVICE_CERTS_H

#define IOT_DEVICE_ID "${DEVICE_ID}"

static const char IOT_ROOT_CA_CERT[] = 
$(sed 's/.*/    "&\\n"/' "${CA_CRT}");

static const char IOT_DEVICE_CLIENT_CERT[] = 
$(sed 's/.*/    "&\\n"/' "${CLIENT_CRT}");

static const char IOT_DEVICE_PRIVATE_KEY[] = 
$(sed 's/.*/    "&\\n"/' "${CLIENT_KEY}");

#endif // DEVICE_CERTS_H
EOF

echo "  -> Cヘッダ生成完了!"
echo "========================================="
echo "証明書生成が正常に完了しました。"
echo "出力先: ${OUTPUT_DIR}"
echo "========================================="
