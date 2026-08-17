# IoT Platform プロジェクト スナップショット情報 (v1.0.0)

**作成日時**: 2026-08-17 14:06:00  
**バージョン**: `v1.0.0 (Master Release)`  
**Git コミット ID**: `b3cd709` (Tag: `v1.0.0`)  
**アーカイブファイル**: `../iot-platform-project-v1.0.0-snapshot.zip`

---

## 1. スナップショット同梱コンポーネント一覧

| コンポーネント | 格納ディレクトリ | 概要 |
| :--- | :--- | :--- |
| **総合仕様書** | [`docs/SYSTEM_SPECIFICATION.md`](docs/SYSTEM_SPECIFICATION.md), [`docs/system_specification.html`](docs/system_specification.html) | 全機能・アーキテクチャ・ハードウェア結線・APIリファレンス |
| **セキュリティ基盤** | [`tools/certs/`](tools/certs/) | プライベート Root CA、mTLS 証明書発行スクリプト、Cヘッダ自動生成 |
| **エッジ FW (HAL/OSAL)** | [`edge/`](edge/) | FreeRTOS OSAL, PC Mock, STM32 (ARM GCC), ESP32-S3 (PlatformIO/Arduino) |
| **高効率通信 & バッファ** | [`edge/middleware/`](edge/middleware/) | Protocol Buffers v3 シリアライザ（約81%削減）, 32件オフラインリングバッファ |
| **IoT 管理サーバ** | [`server/`](server/) | Go製 mTLS サーバ (:8443), SQLite WAL 時系列DB, 双方向 C2, 異常検知 & Webhook |
| **Web ダッシュボード** | [`server/web/`](server/web/) | Chart.js リアルタイム時系列グラフ, リモートコマンド制御パネル (:8080) |
| **テスト & CI/CD** | [`test_all.bat`](test_all.bat), [`.github/workflows/ci.yml`](.github/workflows/ci.yml), [`docker-compose.yml`](docker-compose.yml) | ローカル一括テストスイート、GitHub Actions 5ステージワークフロー |

---

## 2. スナップショットの復元 & クイックスタート手順

### ① ソースコードの展開
```powershell
# ZIP ファイルから展開する場合
Expand-Archive -Path "iot-platform-project-v1.0.0-snapshot.zip" -DestinationPath "iot-platform-project"
```

### ② 全自動テストの実行
```powershell
# 証明書生成 -> エッジ単体 -> サーバ/DB/Webhook -> E2E/OTA/C2 テストを一括実行
.\test_all.bat
```

### ③ サーバの起動 & Web ダッシュボードアクセス
```powershell
cd server
go run main.go
# -> ブラウザで http://localhost:8080/ にアクセス (仕様書は /spec で閲覧可能)
```
