package alerting

import (
	"fmt"
	"iot-platform-server/storage"
	"iot-platform-server/storage/models"
	"log"
	"time"
)

type RuleThresholds struct {
	HighTempThreshold float64
	LowTempThreshold  float64
	LowBatteryPct     uint32
	WeakRSSIThreshold int32
}

type RuleEngine struct {
	thresholds RuleThresholds
	webhook    *WebhookClient
	repo       storage.Repository
}

func NewRuleEngine(thresholds RuleThresholds, webhookURL string, repo storage.Repository) *RuleEngine {
	return &RuleEngine{
		thresholds: thresholds,
		webhook:    NewWebhookClient(webhookURL),
		repo:       repo,
	}
}

func DefaultThresholds() RuleThresholds {
	return RuleThresholds{
		HighTempThreshold: 35.0, // 35度以上で高温警告
		LowTempThreshold:  0.0,  // 0度未満で凍結警告
		LowBatteryPct:     20,   // 20%未満でバッテリ警告
		WeakRSSIThreshold: -85,  // -85dBm未満で弱電波警告
	}
}

func (e *RuleEngine) EvaluateAndAlert(t *models.TelemetryPayload) []AlertEvent {
	var alerts []AlertEvent
	now := time.Now().UTC().Unix()

	// 1. 高温アラート
	if t.Metrics.Temperature > e.thresholds.HighTempThreshold {
		alerts = append(alerts, AlertEvent{
			AlertID:    fmt.Sprintf("alt-temp-high-%d", now),
			DeviceID:   t.Header.DeviceID,
			RuleName:   "HIGH_TEMPERATURE",
			Severity:   SeverityCritical,
			Title:      "高温アラート検知",
			Message:    fmt.Sprintf("温度が閾値 (%.1f℃) を超過しました: %.2f℃", e.thresholds.HighTempThreshold, t.Metrics.Temperature),
			CurrentVal: t.Metrics.Temperature,
			Threshold:  e.thresholds.HighTempThreshold,
			Timestamp:  now,
		})
	}

	// 2. 凍結・低温アラート
	if t.Metrics.Temperature < e.thresholds.LowTempThreshold {
		alerts = append(alerts, AlertEvent{
			AlertID:    fmt.Sprintf("alt-temp-low-%d", now),
			DeviceID:   t.Header.DeviceID,
			RuleName:   "LOW_TEMPERATURE",
			Severity:   SeverityWarning,
			Title:      "低温・凍結注意アラート",
			Message:    fmt.Sprintf("温度が氷点下 (%.1f℃) を下回りました: %.2f℃", e.thresholds.LowTempThreshold, t.Metrics.Temperature),
			CurrentVal: t.Metrics.Temperature,
			Threshold:  e.thresholds.LowTempThreshold,
			Timestamp:  now,
		})
	}

	// 3. バッテリ残量低下アラート
	if t.Metrics.BatteryLevelPct > 0 && t.Metrics.BatteryLevelPct < e.thresholds.LowBatteryPct {
		alerts = append(alerts, AlertEvent{
			AlertID:    fmt.Sprintf("alt-batt-low-%d", now),
			DeviceID:   t.Header.DeviceID,
			RuleName:   "LOW_BATTERY",
			Severity:   SeverityCritical,
			Title:      "バッテリ残量低下アラート",
			Message:    fmt.Sprintf("バッテリ残量が %d%% (閾値: %d%%) です。早期充電/交換が必要です", t.Metrics.BatteryLevelPct, e.thresholds.LowBatteryPct),
			CurrentVal: t.Metrics.BatteryLevelPct,
			Threshold:  e.thresholds.LowBatteryPct,
			Timestamp:  now,
		})
	}

	// 4. 電波微弱アラート
	if t.Metrics.RSSI < e.thresholds.WeakRSSIThreshold {
		alerts = append(alerts, AlertEvent{
			AlertID:    fmt.Sprintf("alt-rssi-weak-%d", now),
			DeviceID:   t.Header.DeviceID,
			RuleName:   "WEAK_SIGNAL",
			Severity:   SeverityWarning,
			Title:      "電波強度低下アラート",
			Message:    fmt.Sprintf("RSSIが %ddBm (閾値: %ddBm) に低下しています", t.Metrics.RSSI, e.thresholds.WeakRSSIThreshold),
			CurrentVal: t.Metrics.RSSI,
			Threshold:  e.thresholds.WeakRSSIThreshold,
			Timestamp:  now,
		})
	}

	// アラートの保存 & Webhook 送信
	for _, alt := range alerts {
		log.Printf("[ALERT TRIGGERED] [%s] %s for device %s", alt.Severity, alt.Title, alt.DeviceID)

		// DB の events テーブルに記録
		if e.repo != nil {
			_ = e.repo.SaveEvent(&models.EventPayload{
				Header: t.Header,
				Event: models.EventDetail{
					EventType: alt.RuleName,
					Severity:  string(alt.Severity),
					Message:   alt.Message,
					Details: map[string]interface{}{
						"current_value": alt.CurrentVal,
						"threshold":     alt.Threshold,
					},
				},
			})
		}


		// Webhook へ非同期送信
		e.webhook.SendAlertAsync(alt)
	}

	return alerts
}
