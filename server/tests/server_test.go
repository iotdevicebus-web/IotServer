package tests

import (
	"bytes"
	"crypto/tls"
	"crypto/x509"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"iot-platform-server/api/handlers"
	"iot-platform-server/api/middleware"
	"iot-platform-server/core/alerting"
	"iot-platform-server/core/telemetry"
	"iot-platform-server/storage"
	"iot-platform-server/storage/models"
	"math"
	"net/http"
	"net/http/httptest"
	"os"
	"testing"
	"time"
)


func TestProtobufDecoder(t *testing.T) {
	// テスト用 Protobuf バイナリの構築 (TelemetryRequest)
	// Header: device_id="DEV-ESP32-001", seq_no=1, fw="1.0.0"
	var headerBuf bytes.Buffer
	// Field 1: device_id (DEV-ESP32-001)
	headerBuf.Write([]byte{0x0a, 0x0d})
	headerBuf.WriteString("DEV-ESP32-001")
	// Field 3: seq_no = 1
	headerBuf.Write([]byte{0x18, 0x01})
	// Field 4: fw = 1.0.0
	headerBuf.Write([]byte{0x22, 0x05})
	headerBuf.WriteString("1.0.0")

	// Metrics: temp=25.5, humi=60.0, batt=3.95, level=90, rssi=-65
	var metricsBuf bytes.Buffer
	// Field 1: temp (float32=25.5) -> 0x41cc0000
	metricsBuf.WriteByte(0x0d)
	binary.Write(&metricsBuf, binary.LittleEndian, math.Float32bits(25.5))
	// Field 2: humidity (float32=60.0) -> 0x42700000
	metricsBuf.WriteByte(0x15)
	binary.Write(&metricsBuf, binary.LittleEndian, math.Float32bits(60.0))
	// Field 3: batt (float32=3.95)
	metricsBuf.WriteByte(0x1d)
	binary.Write(&metricsBuf, binary.LittleEndian, math.Float32bits(3.95))
	// Field 4: level = 90
	metricsBuf.Write([]byte{0x20, 0x5a})
	// Field 5: rssi = -65
	metricsBuf.Write([]byte{0x28, 0xbf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01})

	// ルートメッセージの組み立て
	var rootBuf bytes.Buffer
	// Tag 1 (header)
	rootBuf.WriteByte(0x0a)
	rootBuf.WriteByte(byte(headerBuf.Len()))
	rootBuf.Write(headerBuf.Bytes())
	// Tag 2 (metrics)
	rootBuf.WriteByte(0x12)
	rootBuf.WriteByte(byte(metricsBuf.Len()))
	rootBuf.Write(metricsBuf.Bytes())

	payload, err := telemetry.DecodeTelemetryProtobuf(rootBuf.Bytes())
	if err != nil {
		t.Fatalf("Protobuf decoding failed: %v", err)
	}

	if payload.Header.DeviceID != "DEV-ESP32-001" {
		t.Errorf("Expected DeviceID DEV-ESP32-001, got %s", payload.Header.DeviceID)
	}
	if payload.Header.SeqNo != 1 {
		t.Errorf("Expected SeqNo 1, got %d", payload.Header.SeqNo)
	}
	if math.Abs(payload.Metrics.Temperature-25.5) > 0.01 {
		t.Errorf("Expected Temp 25.5, got %f", payload.Metrics.Temperature)
	}
	if math.Abs(payload.Metrics.Humidity-60.0) > 0.01 {
		t.Errorf("Expected Humidity 60.0, got %f", payload.Metrics.Humidity)
	}

	fmt.Printf("Protobuf Decoder Test Passed! Binary Size: %d bytes (vs JSON ~290 bytes)\n", rootBuf.Len())
}

func TestSQLRepository(t *testing.T) {
	dbPath := "test_iot_platform.db"
	os.Remove(dbPath)
	defer os.Remove(dbPath)

	repo, err := storage.NewSQLRepository("sqlite", dbPath)
	if err != nil {
		t.Fatalf("Failed to initialize SQLite repo: %v", err)
	}

	payload := &models.TelemetryPayload{
		Header: models.MessageHeader{
			DeviceID:        "DEV-ESP32-001",
			Timestamp:       1755421000,
			SeqNo:           1,
			FirmwareVersion: "1.0.0",
			BootCount:       1,
		},
		Metrics: models.MetricsData{
			Temperature:     24.8,
			Humidity:        52.3,
			BatteryVoltage:  3.98,
			BatteryLevelPct: 95,
			RSSI:            -60,
		},
		Status: &models.DeviceStatus{
			State:         "NORMAL",
			UptimeSec:     100,
			FreeHeapBytes: 46000,
		},
	}

	if err := repo.SaveTelemetry(payload); err != nil {
		t.Fatalf("SaveTelemetry failed: %v", err)
	}

	dev, err := repo.GetDevice("DEV-ESP32-001")
	if err != nil {
		t.Fatalf("GetDevice failed: %v", err)
	}
	if dev.DeviceID != "DEV-ESP32-001" || dev.TotalTelemetries != 1 || dev.Status != "ONLINE" {
		t.Errorf("Unexpected device state: %+v", dev)
	}

	hist, err := repo.GetTelemetryHistory("DEV-ESP32-001", 10)
	if err != nil {
		t.Fatalf("GetTelemetryHistory failed: %v", err)
	}
	if len(hist) != 1 {
		t.Errorf("Expected 1 historical record, got %d", len(hist))
	}
}

func TestRuleEngineAndWebhook(t *testing.T) {
	// モック Webhook サーバの作成
	receivedWebhook := make(chan alerting.AlertEvent, 5)
	mockServer := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var payload alerting.WebhookPayload
		_ = json.NewDecoder(r.Body).Decode(&payload)
		receivedWebhook <- payload.Alert
		w.WriteHeader(http.StatusOK)
	}))
	defer mockServer.Close()

	repo := storage.NewInMemoryRepository()
	thresholds := alerting.DefaultThresholds()
	engine := alerting.NewRuleEngine(thresholds, mockServer.URL, repo)

	// 1. 高温テレメトリ (42.5℃ > 35.0℃)
	highTempPayload := &models.TelemetryPayload{
		Header: models.MessageHeader{
			DeviceID:  "DEV-ESP32-001",
			Timestamp: time.Now().Unix(),
			SeqNo:     1,
		},
		Metrics: models.MetricsData{
			Temperature:     42.5,
			Humidity:        50.0,
			BatteryVoltage:  3.9,
			BatteryLevelPct: 80,
			RSSI:            -60,
		},
	}

	alerts := engine.EvaluateAndAlert(highTempPayload)
	if len(alerts) != 1 || alerts[0].RuleName != "HIGH_TEMPERATURE" {
		t.Fatalf("Expected 1 HIGH_TEMPERATURE alert, got %d", len(alerts))
	}

	// Webhook 受信確認 (非同期待ち)
	select {
	case alert := <-receivedWebhook:
		if alert.DeviceID != "DEV-ESP32-001" || alert.Severity != alerting.SeverityCritical {
			t.Errorf("Unexpected webhook alert payload: %+v", alert)
		}
		fmt.Printf("Webhook Alert Successfully Received: [%s] %s\n", alert.Severity, alert.Title)
	case <-time.After(2 * time.Second):
		t.Fatal("Timeout waiting for Webhook dispatch")
	}

	// 2. バッテリ低下 (10% < 20%)
	lowBattPayload := &models.TelemetryPayload{
		Header: models.MessageHeader{
			DeviceID:  "DEV-STM32-002",
			Timestamp: time.Now().Unix(),
			SeqNo:     2,
		},
		Metrics: models.MetricsData{
			Temperature:     24.0,
			Humidity:        50.0,
			BatteryVoltage:  3.3,
			BatteryLevelPct: 10,
			RSSI:            -60,
		},
	}

	alertsBatt := engine.EvaluateAndAlert(lowBattPayload)
	if len(alertsBatt) != 1 || alertsBatt[0].RuleName != "LOW_BATTERY" {
		t.Fatalf("Expected LOW_BATTERY alert, got %+v", alertsBatt)
	}

	select {
	case alert := <-receivedWebhook:
		if alert.RuleName != "LOW_BATTERY" {
			t.Errorf("Expected LOW_BATTERY webhook, got %s", alert.RuleName)
		}
		fmt.Printf("Webhook Low Battery Alert Received: [%s] %s\n", alert.Severity, alert.Title)
	case <-time.After(2 * time.Second):
		t.Fatal("Timeout waiting for Webhook dispatch")
	}
}

func TestTelemetryWithMTLS(t *testing.T) {
	repo := storage.NewInMemoryRepository()
	handler := handlers.NewApiHandler(repo, "")


	mux := http.NewServeMux()
	mtlsMiddleware := middleware.MTLSAuthMiddleware(true)
	mux.Handle("/api/v1/telemetry", mtlsMiddleware(http.HandlerFunc(handler.HandleTelemetry)))

	serverCertPath := "../../tools/certs/out/server.crt"
	serverKeyPath := "../../tools/certs/out/server.key"
	clientCertPath := "../../tools/certs/out/client_DEV-ESP32-001.crt"
	clientKeyPath := "../../tools/certs/out/client_DEV-ESP32-001.key"
	caCertPath := "../../tools/certs/out/ca.crt"

	serverCert, err := tls.LoadX509KeyPair(serverCertPath, serverKeyPath)
	if err != nil {
		t.Fatalf("Failed to load server cert: %v", err)
	}

	caPEM, err := os.ReadFile(caCertPath)
	if err != nil {
		t.Fatalf("Failed to load root CA: %v", err)
	}
	caPool := x509.NewCertPool()
	caPool.AppendCertsFromPEM(caPEM)

	ts := httptest.NewUnstartedServer(mux)
	ts.TLS = &tls.Config{
		Certificates: []tls.Certificate{serverCert},
		ClientCAs:    caPool,
		ClientAuth:   tls.RequireAndVerifyClientCert,
	}
	ts.StartTLS()
	defer ts.Close()

	clientCert, err := tls.LoadX509KeyPair(clientCertPath, clientKeyPath)
	if err != nil {
		t.Fatalf("Failed to load client cert: %v", err)
	}

	clientTLS := &tls.Config{
		Certificates: []tls.Certificate{clientCert},
		RootCAs:      caPool,
	}
	client := &http.Client{
		Transport: &http.Transport{TLSClientConfig: clientTLS},
	}

	reqBody := models.TelemetryPayload{
		Header: models.MessageHeader{
			DeviceID:        "DEV-ESP32-001",
			Timestamp:       1755420000,
			SeqNo:           1,
			FirmwareVersion: "1.0.0",
		},
		Metrics: models.MetricsData{
			Temperature:     26.5,
			Humidity:        55.0,
			BatteryVoltage:  3.92,
			BatteryLevelPct: 90,
			RSSI:            -65,
		},
	}
	data, _ := json.Marshal(reqBody)

	resp, err := client.Post(ts.URL+"/api/v1/telemetry", "application/json", bytes.NewReader(data))
	if err != nil {
		t.Fatalf("mTLS Request failed: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		t.Fatalf("Expected 200 OK, got %d. Body: %s", resp.StatusCode, string(body))
	}
}
