# 📋 プロジェクト作業申し送り状 (Project Handover Document)

**プロジェクト名**: IoT Platform Project (超低消費電力 ゼロトラスト IoT プラットフォーム)  
**作成日時**: 2026-08-18 15:45 JST  
**対象マイコン**: Freenove ESP32-S3 WROOM (8MB Flash + 8MB Octal PSRAM) / STM32 (ピュアC99)  
**本番管理サーバ**: GMO itpark (`https://www.gontaro.org/iot/`) (PHP 8.4 + SQLite WAL モード & リアルタイム Web UI)  
**ローカル管理サーバ**: Go 言語 REST / mTLS サーバ (`https://192.168.3.4:8443` & `http://localhost:8080/`)  
**GitHub リポジトリ**: `https://github.com/iotdevicebus-web/IotServer`

---

## 1. 稼働環境 & ネットワーク構成

| 項目 | 設定値 / 仕様 | 備考 |
| :--- | :--- | :--- |
| **本番クラウドサーバ URL** | **https://www.gontaro.org/iot/** | GMO itpark (PHP 8.4 + SQLite) 常時稼働 |
| **本番 API エンドポイント** | **https://www.gontaro.org/iot/api.php** | HTTPS (443) テレメトリ受信・C2制御 |
| **PC ローカルサーバ IP** | **192.168.3.4:8443** (mTLS) / **:8080** (Web) | 証明書 SAN に `gontaro.org` & `192.168.3.4` 登録済み |
| **エッジデバイス IP** | **192.168.3.65** (DHCP) | デバイス固有 ID: **DEV-ESP32-001** |
| **接続 Wi-Fi SSID / PW** | **ControlAdLab** / **ControlAD** | `edge/platform/esp32/AppConst.hpp` で定義 |
| **電子ペーパー (e-Paper)** | **Waveshare 1.54" Rev2.1** | 200x200 SSD1681 白黒 (GxEPD2) 大型フォント表示 |
| **e-Paper 配線 (GPIO)** | **DIN:11, CLK:12, CS:10, DC:9, RST:8, BUSY:7** | FSPI ネイティブ / PSRAM(26-37)競合完全回避 |

---

## 2. 実装完了機能 & 最新アーキテクチャ

### ① GMO クラウド本番サーバ (`gontaro.org/iot/`) の 24時間常時稼働
* **PHP 8.4 + SQLite WAL モード**: 高速かつ信頼性の高いテレメトリ自動永続化。
* **リアルタイム Web ダッシュボード**: `https://www.gontaro.org/iot/` にて温度・湿度・バッテリー推移グラフ、デバイス一覧、稼働状態を常時監視。
* **双方向 C2 動的スリープ制御**: ダッシュボードから「スリープ10秒/60秒」をクリックすると、次回エッジ起床時に自動配信・適用。
* **コマンド待機インジケータ**: コマンド送信待ちの経過秒数（1秒...2秒...）リアルタイム表示 & 送信完了トースト通知。

### ② Waveshare 1.54inch e-Paper (Rev2.1) 大型フォント状態表示 & 超低消費電力
* **HAL 抽象化**: `edge/hal/include/hal_epaper.h` および `edge/platform/esp32/hal_esp32_epaper.cpp` で GxEPD2 制御を完全カプセル化。
* **大型フォント視認性最適化**:
  * 温度・湿度: **`TextSize 3`（高さ 24px の超特大フォント）**
  * 端末ID・電圧・スリープ周期: **`TextSize 2`（高さ 16px の中型フォント）**
  * サーバ通信ステータス枠: 角丸ボックス内に `SERVER: www.gontaro.org` / `200 OK` を強調表示。
* **超低消費電力 Hibernate**: 表示更新後に e-Paper コントローラを休止させ、ESP32 Deep Sleep 中も電力消費ゼロで画面を永続保持。

### ③ 8MB Octal PSRAM (SDRAM) 大容量オフラインバッファ
* `platformio.ini` に `board_build.arduino.memory_type = qio_opi`, `board_build.psram_type = opi`, `-DBOARD_HAS_PSRAM` を設定。
* `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` により **10,000 件（約 1.5MB、約 41 時間〜1 週間分）** のテレメトリをオフライン保持可能。

### ④ 完全イベント駆動 (Zero-Polling) & 厳格な割り込みマスク制御
* **Wi-Fi 接続**: while ポーリングを全廃し、`WiFi.onEvent(on_wifi_event)` と FreeRTOS バイナリセマフォによる即時起床待機（CPU浪費ゼロ）。
* **GPIO 4 外部スイッチ (Active LOW)**: 起床直後に HAL 経由で即座に割り込みを完全禁止（マスク）➔ Deep Sleep 移行直前にスリープ復帰割り込みを再有効化（チャタリング誤動作・不要ディレイの完全排除）。

### ⑤ 品質管理規約 (QC_check_ESP32.md) 100% 適合
* **Phase 1**: ESP-IDF / Arduino Core 標準 API に完全準拠。
* **Phase 2**: 割り込み・スリープ・e-Paper 制御を HAL（`edge/hal/include/`）に完全カプセル化。
* **Phase 3**: デバッグライトを Caller（setup）から Callee（各関数内部）へ最適配置。
* **Phase 4**: ピン番号・ポート・タイムアウト・容量などのマジックナンバーを `edge/platform/esp32/AppConst.hpp` で constexpr 一元管理。

---

## 3. 全自動テスト・ビルド合格実績 (100% PASS)

```text
=== RUN   TestProtobufDecoder
--- PASS: TestProtobufDecoder (0.00s)
=== RUN   TestSQLRepository
--- PASS: TestSQLRepository (0.03s)
=== RUN   TestRuleEngineAndWebhook
--- PASS: TestRuleEngineAndWebhook (0.00s)
=== RUN   TestTelemetryWithMTLS
--- PASS: TestTelemetryWithMTLS (0.00s)
=== RUN   TestDynamicSleepIntervalPersistence
--- PASS: TestDynamicSleepIntervalPersistence (0.00s)
PASS: ok  iot-platform-server/tests (100% 合格)
```

* **ESP32-S3 ファームウェア**: PlatformIO クリーンビルド **[SUCCESS]** (RAM: 15.7%, Flash: 27.4%)
* **GMO 本番 API**: `https://www.gontaro.org/iot/api.php?endpoint=healthz` -> `{"status":"HEALTHY", ...}` **200 OK**
* **Web ダッシュボード**: `https://www.gontaro.org/iot/` -> **200 OK**

---

## 4. 主要ファイルマップ

```text
x:/iot-platform-project/
├── HANDOVER.md                                 # 本作業申し送り状
├── iot-platform-project-v1.1.0-snapshot.zip     # 全ソースコード最新スナップショット
├── gontaro-iot-upload.zip                       # GMO サーバ公開用パッケージ (Web UI + API)
├── iot_gmo_package/
│   ├── index.html                              # GMO 本番 Web ダッシュボード
│   ├── api.php                                 # GMO 本番 PHP 8.4 REST API
│   ├── test.php                                # 環境診断スクリプト
│   └── .htaccess                               # Apache 設定
├── edge/
│   ├── platform/esp32/
│   │   ├── main.cpp                            # ESP32-S3 エントリポイント (QC規約準拠・完全イベント駆動)
│   │   ├── AppConst.hpp                        # アプリケーション定数一元管理 (GMOエンドポイント・GPIO設定)
│   │   ├── hal_esp32_sleep.c                   # HAL スリープ・割り込み制御実装
│   │   ├── hal_esp32_epaper.cpp                # Waveshare 1.54" e-Paper HAL 実装 (SSD1681 大型フォント)
│   │   └── QC_check_ESP32.md                   # ESP32 品質管理チェック規約
│   ├── hal/include/
│   │   ├── hal_sleep.h                         # HAL スリープ・割り込みヘッダ
│   │   └── hal_epaper.h                        # HAL 電子ペーパーヘッダ
│   └── middleware/serializer/
│       ├── protobuf_serializer.c / .h          # 超軽量 Protobuf v3 シリアライザ
│       └── telemetry_serializer.h              # テレメトリ構造体定義
├── server/
│   ├── main.go                                 # Go 言語 IoT サーバメインエントリ
│   ├── config/config.go                        # サーバ設定 (マルチドメイン証明書自動解決)
│   ├── deploy/                                 # Linux デプロイ用パッケージ & systemd サービス
│   │   ├── install.sh
│   │   └── iot-server.service
│   ├── api/handlers/telemetry_handler.go       # Go REST ハンドラ
│   ├── storage/sql_repository.go               # Go SQLite 実装
│   └── tests/server_test.go                    # ユニット & 結合テスト (100% PASS)
├── tools/certs/                                # 2048-bit RSA / PKCS#1 マルチドメイン mTLS 証明書
│   ├── server.cnf                              # gontaro.org, *.gontaro.org, 192.168.3.4 SAN 登録
│   └── out/device_certs.h                      # エッジ用 Cヘッダ
└── platformio.ini                              # 8MB PSRAM (qio_opi) & GxEPD2 設定
```

---

## 5. クイックスタート・コマンド集

### ① ESP32-S3 ファームウェアの書き込み
```powershell
cd x:\iot-platform-project
C:\Users\iam\.platformio\penv\Scripts\platformio.exe run -t upload
```

### ② Go ローカルサーバの起動 (ローカル検証時)
```powershell
cd x:\iot-platform-project\server
go run main.go
```
* ローカル Web UI: **http://localhost:8080/**

### ③ GMO 本番 Web ダッシュボードの閲覧
👉 **`https://www.gontaro.org/iot/`**

### ④ 全自動テストの実行
```powershell
cd x:\iot-platform-project\server
go test -v ./...
```
