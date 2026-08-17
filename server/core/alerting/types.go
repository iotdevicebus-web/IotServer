package alerting

import "time"

type AlertSeverity string

const (
	SeverityInfo     AlertSeverity = "INFO"
	SeverityWarning  AlertSeverity = "WARNING"
	SeverityCritical AlertSeverity = "CRITICAL"
)

type AlertEvent struct {
	AlertID     string                 `json:"alert_id"`
	DeviceID    string                 `json:"device_id"`
	RuleName    string                 `json:"rule_name"`
	Severity    AlertSeverity          `json:"severity"`
	Title       string                 `json:"title"`
	Message     string                 `json:"message"`
	CurrentVal  interface{}            `json:"current_value"`
	Threshold   interface{}            `json:"threshold"`
	Timestamp   int64                  `json:"timestamp"`
	Details     map[string]interface{} `json:"details,omitempty"`
}

type WebhookPayload struct {
	Text        string       `json:"text"`        // Slack / Mattermost互換
	Content     string       `json:"content"`     // Discord互換
	Alert       AlertEvent   `json:"alert"`
	GeneratedAt time.Time    `json:"generated_at"`
}
