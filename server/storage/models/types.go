package models

import "time"

// MessageHeader メッセージ共通ヘッダ
type MessageHeader struct {
	DeviceID        string `json:"device_id"`
	Timestamp       int64  `json:"timestamp"`
	SeqNo           uint32 `json:"seq_no"`
	FirmwareVersion string `json:"firmware_version"`
	BootCount       uint32 `json:"boot_count,omitempty"`
}

// MetricsData センシング測定値
type MetricsData struct {
	Temperature      float64                `json:"temperature,omitempty"`
	Humidity         float64                `json:"humidity,omitempty"`
	BatteryVoltage   float64                `json:"battery_voltage,omitempty"`
	BatteryLevelPct  uint32                 `json:"battery_level_pct,omitempty"`
	RSSI             int32                  `json:"rssi,omitempty"`
	IntervalSec      uint32                 `json:"interval_sec,omitempty"`
	CustomValues     map[string]interface{} `json:"custom_values,omitempty"`
}

// DeviceStatus デバイス稼働状態
type DeviceStatus struct {
	State         string `json:"state,omitempty"`
	UptimeSec     uint32 `json:"uptime_sec,omitempty"`
	FreeHeapBytes uint32 `json:"free_heap_bytes,omitempty"`
}

// TelemetryPayload テレメトリ送信ペイロード (POST /api/v1/telemetry)
type TelemetryPayload struct {
	Header  MessageHeader `json:"header"`
	Metrics MetricsData   `json:"metrics"`
	Status  *DeviceStatus `json:"status,omitempty"`
}

// EventDetail イベント詳細
type EventDetail struct {
	EventType string                 `json:"event_type"`
	Severity  string                 `json:"severity"`
	Message   string                 `json:"message"`
	Details   map[string]interface{} `json:"details,omitempty"`
}

// EventPayload イベント・アラート送信ペイロード (POST /api/v1/events)
type EventPayload struct {
	Header MessageHeader `json:"header"`
	Event  EventDetail   `json:"event"`
}

// OtaInfo レスポンス内OTA案内情報
type OtaInfo struct {
	Available     bool   `json:"available"`
	TargetVersion string `json:"target_version,omitempty"`
	PackageURL    string `json:"package_url,omitempty"`
	SHA256        string `json:"sha256,omitempty"`
	SizeBytes     uint32 `json:"size_bytes,omitempty"`
	Mandatory     bool   `json:"mandatory,omitempty"`
}

// DeviceCommand レスポンス内保留コマンド
type DeviceCommand struct {
	CommandID string                 `json:"command_id"`
	Action    string                 `json:"action"`
	Params    map[string]interface{} `json:"params,omitempty"`
}

// ApiResponse サーバ共通レスポンス
type ApiResponse struct {
	Status           string          `json:"status"`
	Message          string          `json:"message,omitempty"`
	ServerTime       int64           `json:"server_time"`
	SleepIntervalSec uint32          `json:"sleep_interval_sec,omitempty"`
	OTA              *OtaInfo        `json:"ota,omitempty"`
	Commands         []DeviceCommand `json:"commands,omitempty"`
}

// DeviceState 管理用デバイスエンティティ
type DeviceState struct {
	DeviceID             string    `json:"device_id"`
	FirmwareVersion      string    `json:"firmware_version"`
	LastSeenAt           time.Time `json:"last_seen_at"`
	LastSeqNo            uint32    `json:"last_seq_no"`
	LastMetrics          MetricsData `json:"last_metrics"`
	LastStatus           *DeviceStatus `json:"last_status"`
	Status               string    `json:"status"`
	TotalTelemetries     uint64   `json:"total_telemetries"`
	PendingCommandsCount int      `json:"pending_commands_count"`
	CurrentIntervalSec   uint32   `json:"current_interval_sec"`
}


