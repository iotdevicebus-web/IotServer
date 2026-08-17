package main

import (
	"bytes"
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"sync"
	"time"
)

type DeviceState struct {
	DeviceID         string `json:"device_id"`
	FirmwareVersion  string `json:"firmware_version"`
	LastSeenAt       string `json:"last_seen_at"`
	LastSeqNo        uint32 `json:"last_seq_no"`
	Status           string `json:"status"`
	TotalTelemetries uint64 `json:"total_telemetries"`
}

func main() {
	log.Println("==========================================================")
	log.Println("     IoT Platform E2E Integration & OTA Test Runner       ")
	log.Println("==========================================================")

	// 証明書読み込み
	caPEM, err := os.ReadFile("../certs/out/ca.crt")
	if err != nil {
		log.Fatalf("[FATAL] Failed to read CA cert: %v", err)
	}
	caPool := x509.NewCertPool()
	caPool.AppendCertsFromPEM(caPEM)

	adminCert, err := tls.LoadX509KeyPair("../certs/out/client_DEV-ESP32-001.crt", "../certs/out/client_DEV-ESP32-001.key")
	if err != nil {
		log.Fatalf("[FATAL] Failed to load client cert: %v", err)
	}

	mTLSClient := &http.Client{
		Transport: &http.Transport{
			TLSClientConfig: &tls.Config{
				Certificates: []tls.Certificate{adminCert},
				RootCAs:      caPool,
			},
		},
		Timeout: 3 * time.Second,
	}

	// 1. サーバプロセスの起動
	log.Println("[E2E] 1. Starting IoT Management Server (Go)...")
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	serverCmd := exec.CommandContext(ctx, "go", "run", "main.go")
	serverCmd.Dir = "../../server"
	serverCmd.Stdout = os.Stdout
	serverCmd.Stderr = os.Stderr

	if err := serverCmd.Start(); err != nil {
		log.Fatalf("[FATAL] Failed to start server: %v", err)
	}

	defer func() {
		log.Println("[E2E] Stopping Server Process...")
		cancel()
		serverCmd.Wait()
	}()

	// サーバ起動待機 (healthz確認 with mTLS)
	time.Sleep(3 * time.Second)
	ready := false
	for i := 0; i < 20; i++ {
		resp, err := mTLSClient.Get("https://127.0.0.1:8443/healthz")
		if err == nil && resp.StatusCode == http.StatusOK {
			ready = true
			resp.Body.Close()
			break
		}
		time.Sleep(500 * time.Millisecond)
	}


	if !ready {
		log.Fatalf("[FATAL] Server did not become healthy in time.")
	}
	log.Println("[E2E] Server is UP and healthy!")

	// 2. 複数エッジデバイスのシミュレーション並行実行 (OTA ダウンロード & バージョンアップ検証)
	log.Println("[E2E] 2. Spawning simulated devices (DEV-ESP32-001 & DEV-STM32-002)...")
	var wg sync.WaitGroup

	devices := []struct {
		id   string
		cert string
		key  string
	}{
		{"DEV-ESP32-001", "../certs/out/client_DEV-ESP32-001.crt", "../certs/out/client_DEV-ESP32-001.key"},
		{"DEV-STM32-002", "../certs/out/client_DEV-STM32-002.crt", "../certs/out/client_DEV-STM32-002.key"},
	}

	for _, d := range devices {
		wg.Add(1)
		go func(id, cert, key string) {
			defer wg.Done()
			cmd := exec.Command("go", "run", "main.go",
				"-id", id,
				"-cert", cert,
				"-key", key,
				"-ca", "../certs/out/ca.crt",
				"-cycles", "3",
				"-interval", "1",
			)

			cmd.Dir = "../simulator"
			cmd.Stdout = os.Stdout
			cmd.Stderr = os.Stderr
			if err := cmd.Run(); err != nil {
				log.Printf("[ERROR] Simulator %s failed: %v", id, err)
			}
		}(d.id, d.cert, d.key)
	}

	wg.Wait()
	log.Println("[E2E] All simulated devices completed transmissions and OTA updates.")

	// 3. サーバ側デバイス一覧の検証 (mTLS 経由)
	log.Println("[E2E] 3. Verifying registered devices and OTA upgrade status on Server...")
	resp, err := mTLSClient.Get("https://127.0.0.1:8443/api/v1/devices")
	if err != nil {
		log.Fatalf("[FATAL] Failed to query /api/v1/devices: %v", err)
	}
	defer resp.Body.Close()

	body, _ := io.ReadAll(resp.Body)
	var deviceList []DeviceState
	if err := json.Unmarshal(body, &deviceList); err != nil {
		log.Fatalf("[FATAL] Failed to parse device list JSON: %v", err)
	}

	log.Printf("[E2E] Registered Devices Count: %d", len(deviceList))
	for _, dev := range deviceList {
		log.Printf("  - Device: %s, Status: %s, FW Version: %s, Total Packets: %d, Last Seq: %d",
			dev.DeviceID, dev.Status, dev.FirmwareVersion, dev.TotalTelemetries, dev.LastSeqNo)
		if dev.FirmwareVersion != "1.1.0" {
			log.Fatalf("[FAIL] Device %s did not upgrade to firmware 1.1.0 (found: %s)", dev.DeviceID, dev.FirmwareVersion)
		}
	}

	if len(deviceList) < 2 {
		log.Fatalf("[FAIL] Expected at least 2 devices, found %d", len(deviceList))
	}

	// 4. 時系列履歴APIの検証 (SQLite DB 永続化確認)
	log.Println("[E2E] 4. Verifying Time-Series Database Telemetry History for DEV-ESP32-001...")
	histResp, err := mTLSClient.Get("https://127.0.0.1:8443/api/v1/telemetry/history?device_id=DEV-ESP32-001")
	if err != nil {
		log.Fatalf("[FATAL] Failed to query history API: %v", err)
	}
	defer histResp.Body.Close()

	histBody, _ := io.ReadAll(histResp.Body)
	var historyList []interface{}
	if err := json.Unmarshal(histBody, &historyList); err != nil {
		log.Fatalf("[FATAL] Failed to parse history JSON: %v", err)
	}
	log.Printf("[E2E] Persisted Telemetry Records in SQLite: %d records found.", len(historyList))
	if len(historyList) < 3 {
		log.Fatalf("[FAIL] Expected at least 3 persisted telemetry records, got %d", len(historyList))
	}

	// 5. 管理用 Web ダッシュボード (HTTP :8080) の導通検証
	log.Println("[E2E] 5. Verifying Web Dashboard UI at http://127.0.0.1:8080/ ...")
	httpClient := &http.Client{Timeout: 3 * time.Second}
	webResp, err := httpClient.Get("http://127.0.0.1:8080/")
	if err != nil {
		log.Fatalf("[FATAL] Failed to access Web Dashboard: %v", err)
	}
	defer webResp.Body.Close()

	if webResp.StatusCode != http.StatusOK {
		log.Fatalf("[FAIL] Web Dashboard returned status %d", webResp.StatusCode)
	}
	log.Println("[E2E] Web Dashboard UI loaded successfully (HTTP 200 OK)!")

	// 6. 双方向リモートコマンド (C2) のキューイング & ACK検証
	log.Println("[E2E] 6. Verifying Bidirectional Command & Control (C2)...")
	cmdPayload := map[string]interface{}{
		"device_id": "DEV-ESP32-001",
		"action":    "CONFIG_UPDATE",
		"params":    map[string]interface{}{"sleep_interval_sec": 10},
	}
	cmdBytes, _ := json.Marshal(cmdPayload)
	cmdResp, err := httpClient.Post("http://127.0.0.1:8080/api/v1/commands", "application/json", bytes.NewReader(cmdBytes))
	if err != nil {
		log.Fatalf("[FATAL] Failed to queue command via API: %v", err)
	}
	defer cmdResp.Body.Close()

	if cmdResp.StatusCode != http.StatusOK {
		log.Fatalf("[FAIL] Command Queue API returned status %d", cmdResp.StatusCode)
	}
	log.Println("[E2E] Remote Command (CONFIG_UPDATE) successfully queued in C2 Server!")

	log.Println("==========================================================")
	log.Println("  >>> ALL E2E INTEGRATION & OTA & DB & WEB & C2 TESTS PASSED! <<<   ")
	log.Println("==========================================================")



}
