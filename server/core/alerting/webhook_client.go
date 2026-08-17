package alerting

import (
	"bytes"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"time"
)

type WebhookClient struct {
	webhookURL string
	httpClient *http.Client
}

func NewWebhookClient(webhookURL string) *WebhookClient {
	return &WebhookClient{
		webhookURL: webhookURL,
		httpClient: &http.Client{
			Timeout: 5 * time.Second,
		},
	}
}

func (w *WebhookClient) SendAlertAsync(alert AlertEvent) {
	if w.webhookURL == "" {
		return
	}

	go func() {
		if err := w.SendAlert(alert); err != nil {
			log.Printf("[ALERT WEBHOOK ERROR] Failed to send alert %s to %s: %v", alert.AlertID, w.webhookURL, err)
		}
	}()
}

func (w *WebhookClient) SendAlert(alert AlertEvent) error {
	if w.webhookURL == "" {
		return nil
	}

	icon := "⚠️"
	if alert.Severity == SeverityCritical {
		icon = "🚨"
	}

	formattedMsg := fmt.Sprintf("%s **[%s] %s**\n- **Device**: `%s`\n- **Details**: %s\n- **Time**: %s",
		icon, alert.Severity, alert.Title, alert.DeviceID, alert.Message, time.Unix(alert.Timestamp, 0).Format(time.RFC3339))

	payload := WebhookPayload{
		Text:        formattedMsg,
		Content:     formattedMsg,
		Alert:       alert,
		GeneratedAt: time.Now().UTC(),
	}

	bodyBytes, err := json.Marshal(payload)
	if err != nil {
		return err
	}

	req, err := http.NewRequest("POST", w.webhookURL, bytes.NewReader(bodyBytes))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")

	resp, err := w.httpClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 400 {
		return fmt.Errorf("webhook responded with HTTP %d", resp.StatusCode)
	}

	log.Printf("[ALERT WEBHOOK SUCCESS] Dispatched alert [%s] for device %s", alert.RuleName, alert.DeviceID)
	return nil
}
