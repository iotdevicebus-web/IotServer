# IoT Platform Project (エッジ・クラウド統合基盤)

エッジデバイス（MCU/RTOS）からIoT管理サーバ（クラウド/オンプレミス）までを一気通貫で設計・実装するIoT統合基盤プロジェクトです。

## ドキュメント一覧

* **[`docs/SYSTEM_SPECIFICATION.md`](docs/SYSTEM_SPECIFICATION.md)**: **【システム総合仕様書 Master Specification】** 全機能・アーキテクチャ・API・ピン配置の完全版
* [`docs/01_system_architecture.md`](docs/01_system_architecture.md): システムアーキテクチャ・通信プロトコル・mTLS・HSMセキュリティ仕様

* [`docs/02_edge_requirements.md`](docs/02_edge_requirements.md): エッジ要件・HAL/OSAL・省電力・オフラインバッファリング仕様
* [`docs/03_server_requirements.md`](docs/03_server_requirements.md): サーバ要件・API仕様・SQLite時系列DB・双方向C2・異常検知ルール仕様
* [`docs/04_benchmark_and_performance_report.md`](docs/04_benchmark_and_performance_report.md): 定量ベンチマーク・メモリフットプリント・消費電力・バッテリ寿命レポート
* [`docs/schemas/`](docs/schemas/): JSON Schema & Protocol Buffers (`iot_message.proto`) 定義

## ディレクトリ構成

```
iot-platform-project/
├── .antigravity/                   # AIワークスペース・エージェント設定
│   └── instructions.md             # プロジェクト共通指示書 (System Instructions)
├── docs/                           # 全体設計・仕様ドキュメント
│   ├── 01_system_architecture.md   # 通信・セキュリティ仕様
│   ├── 02_edge_requirements.md     # エッジ要件・HAL/OSAL・省電力仕様
│   ├── 03_server_requirements.md   # サーバ要件・API仕様・データモデル
│   └── schemas/                    # JSON / Protobuf スキーマ定義
├── edge/                           # エッジファームウェア
│   ├── app/                        # 業務ロジック (ハード・OS非依存)
│   ├── middleware/                 # プロトコル・暗号・シリアライザ
│   ├── osal/                       # OS抽象化レイヤ (FreeRTOS, Zephyr, Mock)
│   ├── hal/                        # ハードウェア抽象化レイヤ
│   ├── platform/                   # マイコン・ボード依存実装 (ESP32, STM32, Mock)
│   └── tests/                      # 単体テスト・モックテスト
├── server/                         # IoT管理サーバ
│   ├── api/                        # エンドポイントハンドラ・認証ミドルウェア
│   ├── core/                       # デバイス管理・テレメトリ・OTA管理
│   ├── storage/                    # DBアクセス層・データモデル
│   ├── security/                   # 証明書検証・暗号化処理
│   ├── config/                     # サーバ設定ファイル
│   └── tests/                      # サーバ単体・結合テスト
└── tools/                          # 支援ツール群
    ├── certs/                      # mTLS証明書生成スクリプト
## クイックスタート & 動作検証

### 1. mTLS 証明書の生成
```powershell
cd tools/certs
.\generate_certs.ps1
```

### 2. エッジファームウェア単体テスト (PCモック)
```cmd
edge\tests\run_tests.bat
```

### 3. IoT管理サーバの起動

#### ローカル直接実行 (Go)
```powershell
cd server
go run main.go
```

#### Docker Compose によるコンテナ起動
```bash
# 1. 証明書の生成 (未生成の場合)
cd tools/certs && ./generate_certs.sh

# 2. 一括コンテナ起動 (mTLS :8443, Web Dashboard :8080)
docker compose up -d --build
```
* **エッジ用 mTLS 受信ポート**: `https://localhost:8443/`
* **管理者 Web ダッシュボード**: `http://localhost:8080/`

### 4. E2E 自動結合テスト (複数エッジ・OTA・DB・Web UI 一括検証)
```powershell
cd tools/test_runner
go run run_e2e.go
```


