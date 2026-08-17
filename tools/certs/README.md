# mTLS 証明書生成ツール (Certificate Management)

ローカル開発環境およびCIテスト環境で利用するプライベート認証局 (Root CA)、サーバ証明書、およびエッジデバイス用クライアント証明書を生成するツールです。

## ディレクトリ構成
- `ca.cnf`: Root CA 用の OpenSSL 設定
- `server.cnf`: サーバ証明書用の OpenSSL 設定 (SAN: localhost, 127.0.0.1, iot-server.local)
- `client.cnf`: クライアント証明書用の OpenSSL 設定
- `generate_certs.ps1`: Windows / PowerShell 用発行スクリプト
- `generate_certs.sh`: Linux / macOS / Bash 用発行スクリプト
- `out/`: 生成された証明書・秘密鍵の出力先 (.gitignore対象)

## 使い方

### PowerShell (Windows)
```powershell
cd tools/certs
# デフォルトデバイス (DEV-ESP32-001) の証明書を発行
.\generate_certs.ps1

# デバイスIDを指定して発行
.\generate_certs.ps1 -DeviceId "DEV-STM32-002" -Days 365
```

### Bash (Linux / macOS)
```bash
cd tools/certs
chmod +x generate_certs.sh
# デフォルトデバイス (DEV-ESP32-001) の証明書を発行
./generate_certs.sh

# デバイスIDを指定して発行
./generate_certs.sh DEV-STM32-002 365
```

## 生成される成果物 (`out/`)
- `ca.key` / `ca.crt`: プライベートRoot CA (秘密鍵 / ルート証明書)
- `server.key` / `server.crt`: サーバ用証明書
- `client_<DeviceId>.key` / `client_<DeviceId>.crt`: クライアント証明書
- `device_certs.h`: C言語エッジファームウェアに直接インクルード可能なヘッダ形式
