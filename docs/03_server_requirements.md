# 03. サーバ要件・API仕様・データモデル仕様書

## 1. サーバアーキテクチャ概要
- **実装言語**: Go (標準パッケージ `crypto/tls`, `net/http` による高並行・低フットプリント設計)
- **セキュリティ方針**: 相互TLS (mTLS) 必須、クライアント証明書とペイロードのデバイスID照合によるゼロトラスト運用。
- **データ永続化層**: 
  - **SQLRepository**: SQLite (WALモード / `busy_timeout=5000ms`) および PostgreSQL / TimescaleDB 対応。
  - **テーブル構成**: `devices`, `telemetries`, `events`, `commands` (C2 制御)。
- **双方向遠隔制御 (C2)**: Web ダッシュボードや REST API からエッジデバイスへの非同期コマンドキューイング & レスポンスピギーバック配信 & ACK 追跡。

---

## 2. データベーススキーマ設計 (DDL)

```sql
-- デバイス管理マスターテーブル (UPSERT運用)
CREATE TABLE devices (
    device_id TEXT PRIMARY KEY,
    firmware_version TEXT NOT NULL,
    status TEXT NOT NULL,
    last_seq_no INTEGER NOT NULL,
    last_seen_at DATETIME NOT NULL,
    total_telemetries INTEGER NOT NULL DEFAULT 0,
    last_temp REAL,
    last_humidity REAL,
    last_voltage REAL,
    last_battery_pct INTEGER,
    last_rssi INTEGER,
    created_at DATETIME NOT NULL
);

-- 時系列テレメトリ履歴テーブル
CREATE TABLE telemetries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    seq_no INTEGER NOT NULL,
    firmware_version TEXT NOT NULL,
    boot_count INTEGER,
    temperature REAL,
    humidity REAL,
    battery_voltage REAL,
    battery_level_pct INTEGER,
    rssi INTEGER,
    state TEXT,
    uptime_sec INTEGER,
    free_heap_bytes INTEGER,
    custom_values_json TEXT,
    created_at DATETIME NOT NULL
);
CREATE INDEX idx_telemetries_device_ts ON telemetries(device_id, timestamp DESC);

-- イベント・アラート履歴テーブル
CREATE TABLE events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    event_type TEXT NOT NULL,
    severity TEXT NOT NULL,
    message TEXT NOT NULL,
    details_json TEXT,
    created_at DATETIME NOT NULL
);
CREATE INDEX idx_events_device_ts ON events(device_id, timestamp DESC);

-- 双方向リモートコマンド (C2) 管理テーブル
CREATE TABLE commands (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    command_id TEXT NOT NULL UNIQUE,
    action TEXT NOT NULL,
    params_json TEXT,
    status TEXT NOT NULL DEFAULT 'PENDING',  -- PENDING, SENT, ACKED
    created_at DATETIME NOT NULL,
    sent_at DATETIME,
    acked_at DATETIME
);
CREATE INDEX idx_commands_device_status ON commands(device_id, status);
```

---

## 3. APIエンドポイント仕様 (HTTPS POST / GET)

| メソッド | パス | 認証 | 説明 |
| :--- | :--- | :--- | :--- |
| `POST` | `/api/v1/telemetry` | mTLS | 定期テレメトリ受信（DB挿入・UPSERT・保留中コマンドのピギーバック配信） |
| `POST` | `/api/v1/events` | mTLS | 異常検知・アラート即時通知 |
| `GET` | `/api/v1/devices` | mTLS/Web | 登録デバイス一覧・最新稼働ステータス取得 |
| `GET` | `/api/v1/telemetry/history` | mTLS/Web | 特定デバイスの時系列履歴取得 (`?device_id=XXX&limit=50`) |
| `POST` | `/api/v1/commands` | Web/API | デバイス向けリモートコマンド登録 (C2) |
| `POST` | `/api/v1/commands/ack` | mTLS/Web | エッジからのコマンド実行完了 (ACK) 報告 |
| `GET` | `/api/v1/ota/download/{ver}` | mTLS | 新ファームウェアバイナリのセキュアダウンロード |
| `GET` | `/healthz` | mTLS/Public | ヘルスチェック |
| `GET` | `/` | Web | リアルタイム監視 & C2 Web ダッシュボード (HTTP :8080) |

---

## 4. 異常検知ルールエンジン & Webhook通知仕様

テレメトリ受信時にリアルタイム評価を行い、閾値超過時に `events` テーブルへ記録しつつ外部 Webhook (Slack / Discord / 汎用エンドポイント) へ即時非同期通知を行う。

### 4.1 デフォルト検知ルール

| ルール名 | 重要度 | 条件 | 動作 |
| :--- | :--- | :--- | :--- |
| `HIGH_TEMPERATURE` | **CRITICAL** | `Temperature > 35.0℃` | 🚨 高温アラート発火 & Webhook通知 |
| `LOW_TEMPERATURE` | **WARNING** | `Temperature < 0.0℃` | ⚠️ 凍結・低温注意アラート発火 |
| `LOW_BATTERY` | **CRITICAL** | `BatteryLevelPct < 20%` | 🚨 バッテリ残量低下警報 |
| `WEAK_SIGNAL` | **WARNING** | `RSSI < -85 dBm` | ⚠️ 電波強度低下アラート |

### 4.2 Webhook 通知ペイロード例 (Slack / Discord 互換)
```json
{
  "text": "🚨 **[CRITICAL] 高温アラート検知**\n- **Device**: `DEV-ESP32-001`\n- **Details**: 温度が閾値 (35.0℃) を超過しました: 42.50℃\n- **Time**: 2026-08-17T01:58:30Z",
  "alert": {
    "alert_id": "alt-temp-high-1786931910",
    "device_id": "DEV-ESP32-001",
    "rule_name": "HIGH_TEMPERATURE",
    "severity": "CRITICAL",
    "current_value": 42.5,
    "threshold": 35.0,
    "timestamp": 1786931910
  }
}
```

