#!/bin/bash
# ==============================================================================
# IoT Platform Management Server - GMO Server 1-Click Installer
# Domain: gontaro.org
# ==============================================================================

set -e

INSTALL_DIR="/opt/iot-server"
SERVICE_FILE="/etc/systemd/system/iot-server.service"

echo "========================================================"
echo "  IoT Platform Server Auto-Installer (gontaro.org)"
echo "========================================================"

# 1. 設置ディレクトリ作成
echo "[1/5] ディレクトリを作成中: $INSTALL_DIR ..."
sudo mkdir -p "$INSTALL_DIR"
sudo mkdir -p "$INSTALL_DIR/certs"
sudo mkdir -p "$INSTALL_DIR/web"
sudo mkdir -p "$INSTALL_DIR/docs"

# 2. ファイルコピー
echo "[2/5] ファイルを配置中..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
sudo cp "$SCRIPT_DIR/iot-server-linux" "$INSTALL_DIR/"
sudo chmod +x "$INSTALL_DIR/iot-server-linux"

if [ -d "$SCRIPT_DIR/web" ]; then
    sudo cp -r "$SCRIPT_DIR/web/"* "$INSTALL_DIR/web/"
fi

if [ -d "$SCRIPT_DIR/docs" ]; then
    sudo cp -r "$SCRIPT_DIR/docs/"* "$INSTALL_DIR/docs/"
fi

if [ -d "$SCRIPT_DIR/certs" ]; then
    sudo cp -r "$SCRIPT_DIR/certs/"* "$INSTALL_DIR/certs/"
fi

# 3. systemd サービス登録
echo "[3/5] systemd サービスを登録中..."
cat << 'EOF' | sudo tee "$SERVICE_FILE" > /dev/null
[Unit]
Description=IoT Platform Management Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/iot-server
ExecStart=/opt/iot-server/iot-server-linux
Restart=always
RestartSec=5

Environment=PORT=8080
Environment=EDGE_PORT=8443
Environment=DB_DRIVER=sqlite
Environment=DB_DSN=iot_platform.db
Environment=REQUIRE_MTLS=true
Environment=SERVER_CERT=certs/server.crt
Environment=SERVER_KEY=certs/server.key
Environment=ROOT_CA_CERT=certs/ca.crt

StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

# 4. systemd サービス有効化 & 起動
echo "[4/5] サービスを起動中..."
sudo systemctl daemon-reload
sudo systemctl enable iot-server
sudo systemctl restart iot-server

# 5. ファイアウォール開放 (ufw / firewalld 自動検出)
echo "[5/5] ポート 8443 / 8080 を開放中..."
if command -v ufw >/dev/null 2>&1; then
    sudo ufw allow 8443/tcp || true
    sudo ufw allow 8080/tcp || true
elif command -v firewall-cmd >/dev/null 2>&1; then
    sudo firewall-cmd --permanent --add-port=8443/tcp || true
    sudo firewall-cmd --permanent --add-port=8080/tcp || true
    sudo firewall-cmd --reload || true
fi

echo "========================================================"
echo "  インストールが正常に完了しました！"
echo "  Web UI:     http://gontaro.org:8080/"
echo "  mTLS Port:  https://gontaro.org:8443/"
echo "  ログ確認:   journalctl -u iot-server -f"
echo "========================================================"
