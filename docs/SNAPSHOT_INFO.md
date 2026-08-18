# IoT Platform プロジェクト スナップショット情報 (v1.1.0 最終版)

**作成日時**: 2026-08-18 15:55:00 JST  
**バージョン**: `v1.1.0 (Master Release)`  
**Git リポジトリ**: `https://github.com/iotdevicebus-web/IotServer`  
**アーカイブファイル**: `../iot-platform-project-v1.1.0-snapshot.zip`

---

## 1. スナップショット同梱コンポーネント一覧

| コンポーネント | 格納ディレクトリ | 概要 |
| :--- | :--- | :--- |
| **作業申し送り状** | [`HANDOVER.md`](HANDOVER.md) | 全稼働環境・仕様・テスト実績・クイックスタート集 |
| **総合仕様書** | [`docs/SYSTEM_SPECIFICATION.md`](docs/SYSTEM_SPECIFICATION.md), [`docs/system_specification.html`](docs/system_specification.html) | 全機能・アーキテクチャ・ハードウェア結線・APIリファレンス |
| **ESP32 取扱説明書** | [`docs/edge_user_guide_esp32.html`](docs/edge_user_guide_esp32.html) | ESP32-S3 / Waveshare e-Paper / 8MB PSRAM 運用マニュアル |
| **サーバ取扱説明書** | [`docs/server_user_guide.html`](docs/server_user_guide.html) | GMO クラウド本番 (`gontaro.org/iot/`) & Go ローカルサーバマニュアル |
| **プロトコル仕様書** | [`docs/protocol_specification.html`](docs/protocol_specification.html) | HTTPS JSON & mTLS Protobuf 通信電文仕様 |
| **GMO 本番パッケージ** | [`iot_gmo_package/`](iot_gmo_package/), [`gontaro-iot-upload.zip`](gontaro-iot-upload.zip) | PHP 8.4 REST API (`api.php`), Web UI (`index.html`) |
| **エッジ FW (HAL/OSAL)** | [`edge/`](edge/) | ESP32-S3 (Waveshare 1.54" e-Paper / 8MB PSRAM), STM32 |
| **IoT 管理サーバ** | [`server/`](server/) | Go製 mTLS サーバ (:8443), SQLite WAL 時系列DB, 双方向 C2 |

---

## 2. クイックスタート手順

### ① ESP32-S3 ファームウェアの書き込み
```powershell
C:\Users\iam\.platformio\penv\Scripts\platformio.exe run -t upload
```

### ② GMO クラウド本番ダッシュボード
👉 **`https://www.gontaro.org/iot/`**

### ③ ローカルサーバ起動 (オフライン検証時)
```powershell
cd server
go run main.go
```
